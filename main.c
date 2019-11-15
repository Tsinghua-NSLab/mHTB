#include <stdio.h>
#include <getopt.h>
#include <rte_common.h>
#include <rte_eal.h>
#include <rte_malloc.h>
#include <rte_mempool.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_mbuf.h>
#include <rte_meter.h>
#include <time.h>

#include "main.h"

#define NB_SCHED_CORE				4 	// the number of cores which do l2fwd
#define MAX_FLOW_NUM_PER_CORE		256	// the max number of flow per core

#define DEFAULT_TOKEN_RATE 			(1 << 31)

static struct flow_class flows[NB_SCHED_CORE][MAX_FLOW_NUM_PER_CORE];
static uint16_t rte_ring_num = 0;

#define RING_SIZE     	 			1024 // Define the default ring_size
static inline struct rte_ring*
new_rte_ring(void) {
	char str[20];
	sprintf(str, "ring_%d", rte_ring_num++);
	struct rte_ring *p = rte_ring_create(str, RING_SIZE, SOCKET_ID_ANY,
	RING_F_SP_ENQ | RING_F_SC_DEQ);
	return p;
}

static inline void clear_pkt_cnt(struct pkt_cnt *pkt_cnt_) {
	pkt_cnt_->byte = 0;
	pkt_cnt_->cnt = 0;
}

static inline void clear_pkt_rate(struct pkt_rate *pkt_rate_node) {
	clear_pkt_cnt(&(pkt_rate_node->rate_now));
	clear_pkt_cnt(&(pkt_rate_node->update_parm.curr_pkt_cnt));
	pkt_rate_node->update_parm.last_cycle_num = rte_rdtsc();
	// set the update parameter
	pkt_rate_node->update_parm.max_cycle_interval = rte_get_tsc_hz() >> 2; // about 250ms, update!
	pkt_rate_node->update_parm.max_update_num.cnt = 1000; // get 1000 packets, update!
	pkt_rate_node->update_parm.max_update_num.byte = (uint64_t) (1 << 30); // get 4M, update!
}

static inline void init_tbf(struct token_bucket_filter *tbf) {
	tbf->ctn_max = 32;
	tbf->curr_token_num = 0;
	tbf->last_cycle_num = rte_rdtsc();
	tbf->max_cycle_interval = 1 << 31; // max time interval is 1<<31 (hz is about 2^31)
	tbf->token_rate = DEFAULT_TOKEN_RATE; 	// 2^15 pkts/s -- 32768
	tbf->queue = new_rte_ring();
}

static inline void init_flow_class(struct flow_class *flow) {
	struct flow_class *p = flow;
#ifdef DEMO_FLOW_COUNT
	clear_pkt_cnt(&(p->pkt_receive));
	clear_pkt_cnt(&(p->pkt_send));
	clear_pkt_cnt(&(p->pkt_drop));
#endif
	init_tbf(&(p->tbf));
	clear_pkt_rate(&(p->pkt_receive_rate));
}

// TODO: speed up by（__attribute__((always_inline))）or #define
// update the token number, fresh the update time
// Attention! rte_rdtsc returns the TimeStamp Counter[https://en.wikipedia.org/wiki/Time_Stamp_Counter]
static inline void update_token_bucket(struct token_bucket_filter *tbf_struc) {
	uint64_t current_cycle_num = rte_rdtsc();
	uint64_t cycle_diff = current_cycle_num - tbf_struc->last_cycle_num;
	if (cycle_diff < tbf_struc->max_cycle_interval) {
		if ((cycle_diff * tbf_struc->token_rate) > (rte_get_tsc_hz())) {
			/***
			 * CPU_frequency(HZ): rte_get_tsc_hz()
			 * time_interval = cycle_diff / HZ;
			 * token_add_num = time_interval*token_rate;
			 * 		These calculation is finished in the 'double' correctly,
			 * 		but token_add_num should be integer;
			 * 		so, we should first make sure the token_add_num > 1
			 * token_add_num > 1 <==> time_interval*token_rate > Hz
			 */
			// TODO: how to speed up the divide?
			//		1 divide need 30 cycles or more, so it is better to change into the '>>'
			// _TODO_: will it be overflow? Check!
			/***
			 * Example:
			 *	 	Test Result: cycle_diff is 6000-10000 ~ 2^12-2^13.3 cycles
			 * 		CPU Hz is 3,591,687,480 ~ 2^31.74 cycles per second
			 * 		100Mbps is about 148800 pkts/s ~ 2^17 pps
			 * 		So, cycle_diff * token_rate is 2^30
			 * calculation:
			 *		if cycle_diff is max_cycle_interval, and token_rate is very large
			 *		cycle_diff is 2^32 (2 seconds), token_rate is 2^30 pps--1Gpps/1Gbps
			 *		the calculation is still correct
			 *
			 * 		last_cycle_num += cycle_diff
			 * 		But the token_add_num is not accurate (drop the fractional part)
			 * 			so it is better to use **cycle_diff = token_add_num * HZ / rate**
			 * 		In fact, maybe it is better to use  **token_add_num * (HZ / rate)**
			 *
			 * 		Will the cycle number be overflow?
			 * 		HZ = 2^32, so it need 2^32s = 136y to be overflowed -- it is OK.
			 */
			uint64_t token_add = (cycle_diff * (tbf_struc->token_rate))
					/ (rte_get_tsc_hz());
			tbf_struc->last_cycle_num += token_add
					* ((rte_get_tsc_hz()) / (tbf_struc->token_rate));

			uint64_t new_num = tbf_struc->curr_token_num + token_add;
			tbf_struc->curr_token_num =
					(new_num < (tbf_struc->ctn_max)) ?
							new_num : tbf_struc->ctn_max;
		}
	} else {
		tbf_struc->curr_token_num = tbf_struc->ctn_max;
		tbf_struc->last_cycle_num = current_cycle_num;
		printf("Updating needs too much time\n");
	}
}

// update the rate calculation
static inline void update_rate(struct pkt_rate *rate, uint64_t pkt_len) {
	struct pkt_cnt *curr_pkt_info = &(rate->update_parm.curr_pkt_cnt);
	curr_pkt_info->cnt++;
	curr_pkt_info->byte += pkt_len;
	uint64_t cycle_interval = rte_rdtsc() - rate->update_parm.last_cycle_num;
	struct pkt_cnt *max_pkt_info = &(rate->update_parm.max_update_num);
	struct pkt_cnt *pkt_rate_record = &(rate->rate_now);
	if( 1 ){
		/**
		 * Update rate in a narrow time
		 */
		if (cycle_interval >= rate->update_parm.max_cycle_interval
				|| curr_pkt_info->byte >= max_pkt_info->byte
				|| curr_pkt_info->cnt >= max_pkt_info->cnt) {
			// TODO: speed up with '>>' instead of '/'
			pkt_rate_record->byte = curr_pkt_info->byte
					* (rte_get_tsc_hz() / cycle_interval); // per second bytes
			pkt_rate_record->cnt = curr_pkt_info->cnt
					* (rte_get_tsc_hz() / cycle_interval); // per second packets
			rate->update_parm.last_cycle_num = rte_rdtsc();
			clear_pkt_cnt(curr_pkt_info);
			// DPDK_DBG_OUT("Update rate!\n");
		}
	}else{
		/**
		 * Update rate with a large time - 1s
		 */
		if (cycle_interval >= rte_get_tsc_hz()) {
					pkt_rate_record->byte = curr_pkt_info->byte; // per second bytes
					pkt_rate_record->cnt = curr_pkt_info->cnt; // per second packets
					rate->update_parm.last_cycle_num = rte_rdtsc();
					clear_pkt_cnt(curr_pkt_info);
					// DPDK_DBG_OUT("Update rate!\n");
		}
	}
}

// TODO: make the tbf police more complex
enum tbf_mode {
	TBF_DIRECT_SEND = 0, TBF_DROP = 1,
//TBF_EXCESS = 2,
};

static inline enum tbf_mode tbf_dequeue(struct token_bucket_filter *tbf_struc,
		uint64_t req_token_num) {
	if (tbf_struc->curr_token_num >= req_token_num) {
		tbf_struc->curr_token_num -= req_token_num;
		return TBF_DIRECT_SEND;
	} else {
		return TBF_DROP;
	}
}

static uint8_t port_rx;
static uint8_t port_tx;

#define NB_TX_QUEUE	        NB_SCHED_CORE

struct rte_eth_dev_tx_buffer *tx_buffer[NB_TX_QUEUE];

// dequeue a pkt from a flow -- send or drop
static inline void pkt_dequeue_flow(struct flow_class *flow_node) {
	struct rte_mbuf *pkt;
	struct rte_ring *rx_ring = flow_node->tbf.queue;
	if (rte_ring_dequeue(rx_ring, (void *) &pkt) == 0) {
		// TODO: split the structure into : 'enqueue pkt' and 'dequeue pkt'
		// Now it is naive! When the packet enqueue, if there are enough tokens, then send; otherwise, drop

		// uint64_t pkt_len = rte_pktmbuf_pkt_len(pkt) - sizeof(struct ether_hdr);

#ifdef DEMO_FLOW_COUNT
		// update the flow's receive record
		flow_node->pkt_receive.cnt++;
		flow_node->pkt_receive.byte += pkt_len;
#endif
		// get the method to deal with the packet
		struct token_bucket_filter *tbf_struc = &(flow_node->tbf);
		update_token_bucket(tbf_struc);
		enum tbf_mode tbf_res = tbf_dequeue(tbf_struc, 1);
		if (tbf_res == TBF_DROP) {
#ifdef DEMO_FLOW_COUNT
			flow_node->pkt_send.cnt++;
			flow_node->pkt_send.byte += pkt_len;
#endif
			rte_pktmbuf_free(pkt);
		} else {
#ifdef DEMO_FLOW_COUNT
			flow_node->pkt_drop.cnt++;
			flow_node->pkt_drop.byte += pkt_len;
#endif
			uint16_t sendNum = rte_eth_tx_buffer(port_tx, flow_node->queue_id,
					flow_node->buffer, pkt);
			if( sendNum > 0 ){
				/* N > 0 = packet has been buffered, and the buffer was subsequently flushed,
				 * causing N packets to be sent, and the error callback to be called for
				 * the rest.
				 */
				//DPDK_DBG_OUT("SEND PACKETS WRONG!\n");
			}
		}
	}
}
//Update the parameter of flow_class
static inline void update_rate_param(void) {
	// for each flow
	uint32_t pps_num = 20000;
	time_t timep;
	time(&timep);
	printf("Time:%s", ctime(&timep));
	for (int i = 0; i < NB_SCHED_CORE; i++) {
		for (int j = 0; j < MAX_FLOW_NUM_PER_CORE; j++) {
			struct flow_class *p = &flows[i][j];
			struct token_bucket_filter *tbf = &(p->tbf);
			tbf->token_rate = pps_num;
			// max is 14,888,064 pps
			if (i == 0 && j == 0) {
				printf("Flow [%d][%d], rate %d pps, %ld bit/s\n\n", i, j,
						p->pkt_receive_rate.rate_now.cnt,
						p->pkt_receive_rate.rate_now.byte);
				// print out each flow's speed
			}
		}
	}
//#define NB_FLOW_JUDGE		4
//	struct flow_class *flow_judge[NB_FLOW_JUDGE];
//	flow_judge[0] = &flows[0][0];
//	flow_judge[1] = &flows[0][1];
//	flow_judge[2] = &flows[1][0];
//	flow_judge[3] = &flows[1][1];
//
//	uint32_t *set_rate[NB_FLOW_JUDGE];
//	uint32_t demand_rate[NB_FLOW_JUDGE];
//
//	for( int i=0; i <NB_FLOW_JUDGE; i++){
//		demand_rate[i] = flow_judge[i]->pkt_receive_rate.rate_now.cnt;
//		set_rate[i] = &(flow_judge[i]->tbf.token_rate);
//	}
//	for( int i=0; i <NB_FLOW_JUDGE; i++){
//		*(set_rate[0]) = demand_rate[0];
//	}
	printf("--------------------------------\n");
	sleep(1);
}

// initialize the application
static inline void init_app(void) {
	// for each flow
	for (int i = 0; i < NB_SCHED_CORE; i++) {
		for (int j = 0; j < MAX_FLOW_NUM_PER_CORE; j++) {
			struct flow_class *p = &flows[i][j];
			// init each flow parameter
			init_flow_class(p);
			{
				// set the receive rate measurement parameter
				// Measure for 250ms, update!
				p->pkt_receive_rate.update_parm.max_cycle_interval =
						(rte_get_tsc_hz() >> 4);
				// Or get 1000 packets, update!
				p->pkt_receive_rate.update_parm.max_update_num.cnt = 1000;
				// Or get 4Mbits, update!
				p->pkt_receive_rate.update_parm.max_update_num.byte =
						(uint64_t) (1 << 30);
			}
			{
				// set the token bucket filter parameter, that is, the rate limit
				struct token_bucket_filter * tbf = &(p->tbf);
				tbf->ctn_max = 1024;
				tbf->curr_token_num = 0;
				tbf->last_cycle_num = rte_rdtsc();
				tbf->max_cycle_interval = 1 << 31; // max time interval is 1<<31 (hz is about 2^31)
				tbf->token_rate = DEFAULT_TOKEN_RATE; 	// 2^15 pkts/s -- 32768
			}
			{
				// set the queue id and the tx buffer
				// the two parameter is for the 'send' action when dequeuing a packet
				p->buffer = tx_buffer[i];
				p->queue_id = i;
			}

		}
	}
}

static uint64_t into_sch_core_no = 0;
static uint64_t into_flow_no = 0;
static uint64_t cnt_pkt_num = 0;
// Classify packet into a flow
static inline void pkt_enqueue_flows(struct rte_mbuf *pkt) {
	uint32_t pkt_len = rte_pktmbuf_pkt_len(pkt) - sizeof(struct ether_hdr);
	// Use the location 33 is the destination ip final 8 bits
	// Find the flow to enqueue
	// Core number and flow number
	cnt_pkt_num ++;
	into_sch_core_no = (cnt_pkt_num >> 9) & 0x3;
	into_flow_no = cnt_pkt_num & 0xff;
	//into_sch_core_no = 0; into_flow_no = 0;
	//printf("Core:%d,Flow:%d\n",into_sch_core_no,into_flow_no);
	struct flow_class *flow_node = &flows[into_sch_core_no][into_flow_no];
	uint8_t ret = rte_ring_enqueue(flow_node->tbf.queue, (void *) pkt);
	// _TODO_ if success, update the rate measurement
	update_rate(&(flow_node->pkt_receive_rate), pkt_len);
	if (ret == EDQUOT) {
		/**
		 * - 0: Success; objects enqueued.
		 * -EDQUOT: Quota exceeded. The objects have been enqueued, but the
		 *     high water mark is exceeded.
		 * -ENOBUFS: Not enough room in the ring to enqueue; no object is enqueued.
		 */
		// Out the wrong
		printf("Core:%d, EDQUOT\n", into_sch_core_no);
	}else if(ret == ENOBUFS){
		printf("Core:%d, ENOBUFS\n", into_sch_core_no);
	}
}

/**
 * -------------------------------------------
 * NIC configuration start
 */
static struct rte_eth_conf port_conf = { .rxmode = { .mq_mode = ETH_MQ_RX_RSS,
		.max_rx_pkt_len = ETHER_MAX_LEN, .split_hdr_size = 0, .header_split = 0,
		.hw_ip_checksum = 1, .hw_vlan_filter = 0, .jumbo_frame = 0,
		.hw_strip_crc = 0, }, .rx_adv_conf = { .rss_conf = { .rss_key = NULL,
		.rss_hf = ETH_RSS_IP, }, }, .txmode = { .mq_mode = ETH_DCB_NONE, }, };
#define NIC_RX_QUEUE_DESC               128
#define NIC_TX_QUEUE_DESC               512

#define NIC_RX_QUEUE                    0
#define NIC_TX_QUEUE                    0

/**
 * NIC configuration finish
 * -------------------------------------------------
 */
#define PKT_REC_BURST 32
static struct rte_mbuf *pkts_rx[PKT_REC_BURST];

static __attribute__((noreturn)) int main_loop(
		__attribute__((unused)) void *dummy) {
	uint32_t lcore_id = rte_lcore_id();
	/** TODO:
	 * pkt_cnt is used for each core's number's calculate, for speed, drop
	 * //struct pkt_cnt pkt_cnt;
	 * //clear_pkt_cnt(&pkt_cnt);
	 */
	if (lcore_id == 0) {
		printf("Core %u for rx: port RX = %d, port TX = %d\n", lcore_id,
				port_rx, port_tx);
		// TODO:  Use stack instead of heap to speed up
		int i, nb_rx;
		while (1) {
			nb_rx = rte_eth_rx_burst(port_rx, NIC_RX_QUEUE, pkts_rx,
			PKT_REC_BURST);
			/* Classify packets to different packets*/
			// nb_rx = 0? no packet? why?
			for (i = 0; i < nb_rx; i++) {
				pkt_enqueue_flows(pkts_rx[i]);
				//DPDK_DBG_OUT("Enqueue packet.\n");
			}
		}
	} else if (lcore_id == 1) {
		printf("Core %u for schedule update\n", lcore_id);
		while (1) {
			update_rate_param();
		}
	} else {
		uint32_t ring_id = lcore_id % NB_SCHED_CORE;
		printf("Core %u for RX_RING[%d]:\n", lcore_id, ring_id);
		while (1) {
			// dequeue one packet from each flow
			for (int j = 0; j < MAX_FLOW_NUM_PER_CORE; j++) {
					struct flow_class * flow_node = &(flows[ring_id][j]);
					pkt_dequeue_flow(flow_node);
			}
		}
	}
}

int main(int argc, char **argv) {
	static struct rte_mempool *pool = NULL;
	uint32_t lcore_id;
	int ret, i;

	/* EAL init */
	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Invalid EAL parameters\n");
	argc -= ret;
	argv += ret;

	port_tx = 0;
	port_rx = 1;

	/**
	 * Buffer pool configuration
	 *
	 */
#define NB_MBUF				8095 // 2^13-1
#define MEMPOOL_CACHE_SIZE  256

	/* Buffer pool init */
	pool = rte_pktmbuf_pool_create("pool", NB_MBUF, MEMPOOL_CACHE_SIZE, 0,
	RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
	if (pool == NULL)
		rte_exit(EXIT_FAILURE, "Buffer pool creation error\n");

	/* NIC init */
	ret = rte_eth_dev_configure(port_rx, 1, 1, &port_conf);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Port %d configuration error (%d)\n", port_rx,
				ret);

	ret = rte_eth_rx_queue_setup(port_rx, 0, NIC_RX_QUEUE_DESC,
			rte_eth_dev_socket_id(port_rx),
			NULL, pool);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Port %d RX queue setup error (%d)\n", port_rx,
				ret);

	ret = rte_eth_tx_queue_setup(port_rx, NIC_TX_QUEUE, NIC_TX_QUEUE_DESC,
			rte_eth_dev_socket_id(port_rx),
			NULL);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Port %d TX queue setup error (%d)\n", port_rx,
				ret);

	ret = rte_eth_dev_configure(port_tx, 1, NB_TX_QUEUE, &port_conf);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Port %d configuration error (%d)\n", port_tx,
				ret);

	ret = rte_eth_rx_queue_setup(port_tx, NIC_RX_QUEUE, NIC_RX_QUEUE_DESC,
			rte_eth_dev_socket_id(port_tx),
			NULL, pool);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Port %d RX queue setup error (%d)\n", port_tx,
				ret);

	for (i = 0; i < NB_TX_QUEUE; i++) {
		ret = rte_eth_tx_queue_setup(port_tx, i, NIC_TX_QUEUE_DESC,
				rte_eth_dev_socket_id(port_tx),
				NULL);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "Port %d TX queue %d setup error (%d)\n",
					port_tx, i, ret);
	}

#define PKT_TX_BURST_MAX                32

	char str[20];
	for (i = 0; i < NB_TX_QUEUE; i++) {
		sprintf(str, "tx_queue_%d", i);
		tx_buffer[i] = rte_zmalloc_socket(str,
				RTE_ETH_TX_BUFFER_SIZE(PKT_TX_BURST_MAX), 0,
				rte_eth_dev_socket_id(port_tx));
		if (tx_buffer[i] == NULL) {
			rte_exit(EXIT_FAILURE, "Port %d TX buffer[%d] allocation error\n",
					port_tx, i);
		}
		rte_eth_tx_buffer_init(tx_buffer[i], PKT_TX_BURST_MAX);
	}

	ret = rte_eth_dev_start(port_rx);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Port %d start error (%d)\n", port_rx, ret);

	ret = rte_eth_dev_start(port_tx);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Port %d start error (%d)\n", port_tx, ret);

	rte_eth_promiscuous_enable(port_rx);
	rte_eth_promiscuous_enable(port_tx);

	init_app();

	/* Launch per-lcore init on every lcore */
	rte_eal_mp_remote_launch(main_loop, NULL, CALL_MASTER);

	RTE_LCORE_FOREACH_SLAVE(lcore_id)
	{
		if (rte_eal_wait_lcore(lcore_id) < 0)
			return -1;
	}

	return 0;
}
