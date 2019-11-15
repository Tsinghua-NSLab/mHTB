/*-
 *   BSD LICENSE
 *
 *   Copyright(c) 2010-2016 Intel Corporation. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <getopt.h>

#include <rte_common.h>
#include <rte_eal.h>
#include <rte_malloc.h>
#include <rte_mempool.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_mbuf.h>
#include <rte_spinlock.h>
#include <rte_ip.h>
#include <rte_ether.h>
#include <rte_log.h>
/*
 * Traffic metering configuration
 *
 */

#include "rate_sensor.h"
#include "flow.h"
#include "htb.h"
#include "main.h"

#include "trTCM.h"


#define APP_PKT_FLOW_POS                33
#define APP_PKT_COLOR_POS               5

#if APP_PKT_FLOW_POS > 64 || APP_PKT_COLOR_POS > 64
#error Byte offset needs to be less than 64
#endif

/*
 * Buffer pool configuration
 *
 ***/
#define NB_MBUF             8095 //8095=2^13-1 8192
#define MEMPOOL_CACHE_SIZE  256

static struct rte_mempool *pool = NULL;

/*
 * NIC configuration
 *
 ***/
static struct rte_eth_conf port_conf = {
	.rxmode = {
		.mq_mode	= ETH_MQ_RX_RSS,
		.max_rx_pkt_len = ETHER_MAX_LEN,
		.split_hdr_size = 0,
		.header_split   = 0,
		.hw_ip_checksum = 1,
		.hw_vlan_filter = 0,
		.jumbo_frame    = 0,
		.hw_strip_crc   = 1,
	},
	.rx_adv_conf = {
		.rss_conf = {
			.rss_key = NULL,
			.rss_hf = ETH_RSS_IP,
		},
	},
	.txmode = {
		.mq_mode = ETH_DCB_NONE,
	},
};

#define NIC_RX_QUEUE_DESC               128
#define NIC_TX_QUEUE_DESC               512

#define NIC_RX_QUEUE                    0
#define NIC_TX_QUEUE                    0

/*
 * Packet RX/TX
 *
 ***/
#define PKT_RX_BURST_MAX                64
#define PKT_TX_BURST_MAX                64
#define TIME_TX_DRAIN                   200000ULL

static uint8_t port_no;
// static uint8_t port_rx;
// static uint8_t port_tx;
static struct rte_mbuf *pkts_rx[PKT_RX_BURST_MAX];
#define NB_TX_QUEUE 3
struct rte_eth_dev_tx_buffer *tx_buffer[NB_TX_QUEUE];

#define SCHEDULER_NUM	2
static struct rte_ring* ring_per_core[SCHEDULER_NUM];

static struct ether_addr l2fwd_src_mac;

#define RING_SIZE     2048
static int
init_app(void)
{
	uint32_t i;
	char str[20];
	for(i=0; i<SCHEDULER_NUM; i++){
		sprintf(str, "ring_%d", i);
		ring_per_core[i] = rte_ring_create(str, RING_SIZE, SOCKET_ID_ANY,
					RING_F_SC_DEQ | RING_F_SP_ENQ );
		// only one consumer but multiple producers
		// | RING_F_SP_ENQ
	}
	rte_eth_macaddr_get(port_no,&l2fwd_src_mac);
	return J_htb_init();
}


static void
l2fwd_mac_updating(struct rte_mbuf *m, unsigned dest_portid, struct ether_addr* src_addr)
{
	struct ether_hdr *eth;
	void *tmp;
	eth = rte_pktmbuf_mtod(m, struct ether_hdr *);
	/* 02:00:00:00:00:xx */
	tmp = &eth->d_addr.addr_bytes[0];
	*((uint64_t *)tmp) = 0x000000000002 + ((uint64_t)dest_portid << 40);

	/* src addr */
	ether_addr_copy(src_addr, &eth->s_addr);
}

static inline uint16_t pkt_classify( struct rte_mbuf *pkt){
	uint8_t *pkt_data = rte_pktmbuf_mtod(pkt, uint8_t *);
	uint32_t dst_ip = IPv4(
			(uint32_t)pkt_data[APP_PKT_FLOW_POS-3],
			(uint32_t)pkt_data[APP_PKT_FLOW_POS-2],
			(uint32_t)pkt_data[APP_PKT_FLOW_POS-1],
			(uint32_t)pkt_data[APP_PKT_FLOW_POS] );
	return (uint16_t)(dst_ip % (APP_FLOWS_MAX));
}

static inline void pkt_dequeue_flow() {
	struct rte_mbuf *pkt;
	uint32_t lcore_id = rte_lcore_id();
	struct rte_ring* rx_ring = ring_per_core[lcore_id % SCHEDULER_NUM];
	if (rte_ring_dequeue(rx_ring, (void *) &pkt) == 0) {
		uint16_t flow_id = pkt_classify(pkt);
		// printf("Need to dequeue packets\n");
		// uint8_t *pkt_data = rte_pktmbuf_mtod(pkt, uint8_t *);
		uint64_t req_token_num = rte_pktmbuf_pkt_len(pkt) - sizeof(struct ether_hdr);
		req_token_num = 1;
		// request token number
		int action_res = J_htb_req_token( flow_id, req_token_num );
		// action_res = SEND;
		if ( action_res == J_METER_GREEN){
			l2fwd_mac_updating(pkt, port_no, &l2fwd_src_mac);
			rte_eth_tx_buffer(port_no, lcore_id % NB_TX_QUEUE, tx_buffer[lcore_id % NB_TX_QUEUE], pkt);
		} else if( action_res == J_METER_YELLOW ){
			//rte_ring_enqueue(rx_ring, (void *)pkt);
			if( 0 ){
				rte_pktmbuf_free(pkt);
			}else{
				l2fwd_mac_updating(pkt, port_no, &l2fwd_src_mac);
				rte_eth_tx_buffer(port_no, lcore_id % NB_TX_QUEUE, tx_buffer[lcore_id % NB_TX_QUEUE], pkt);
			}
		} else if( action_res == J_METER_RED){
			rte_pktmbuf_free(pkt);
		}
	}
}

static uint64_t pkt_overflow = 0;
// Classify packet into a flow
static inline void pkt_enqueue_flows(struct rte_mbuf *pkt) {
	uint32_t pkt_len = rte_pktmbuf_pkt_len(pkt) - sizeof(struct ether_hdr);
	// Use the location 33 is the destination ip final 8 bits
	uint16_t flow_id = pkt_classify(pkt);
	// TODO: how to record the flow_id in the pkt to avoid classification repeatedly
	uint8_t ret = rte_ring_enqueue(ring_per_core[flow_id % SCHEDULER_NUM], (void *) pkt);
	if ( ret != 0 ) {
//		printf("Flow:%d, packet is not enqueued!\n", flow_id);
		pkt_overflow++;
		rte_pktmbuf_free(pkt);
		return;
	}
}

void print_stats(){
	const char clr[] = { 27, '[', '2', 'J', '\0' };
	const char topLeft[] = { 27, '[', '1', ';', '1', 'H','\0' };
	/* Clear screen and move to top left */
	printf("%s%s", clr, topLeft);

	printf("==================================================\n");
	printf("not enqueue: %ld\n",pkt_overflow);
	for( uint8_t i = 0; i < APP_FLOWS_MAX; i++){
		printf("---------------+---------------+-----------------+\n");
		print_htb_leaf_info(i);
	}
	for( uint8_t i = 0; i < APP_PARENT_MAX; i++){
		printf("---------------+---------------+-----------------+\n");
		print_htb_inner_info(i);
	}
	printf("---------------+---------------+-----------------+\n");
	print_htb_root_info();
	printf("==================================================\n");
}

static __attribute__((noreturn)) int
main_loop(__attribute__((unused)) void *dummy)
{
	uint64_t current_time, last_time = rte_rdtsc();
	uint32_t lcore_id = rte_lcore_id();
	uint64_t last_update_time = rte_rdtsc();
	//printf("Core %u: port RX = %d, port TX = %d\n", lcore_id, port_no, port_no);
	while (1) {
		uint64_t time_diff;
		int i, nb_rx;
		/* Mechanism to avoid stale packets in the output buffer */
		if( lcore_id == 0 ){
			/* Read packet burst from NIC RX */
			nb_rx = rte_eth_rx_burst(port_no, 0, pkts_rx, PKT_RX_BURST_MAX);
			for ( i = 0; i < nb_rx; i++ ) {
				pkt_enqueue_flows(pkts_rx[i]);
			}
			current_time = rte_rdtsc();
			time_diff = current_time - last_time;
			if (time_diff > rte_get_tsc_hz()) {
				// print out the info
				print_stats();
				last_time = current_time;
			}
			/* Update the htb structure */
			time_diff = current_time - last_update_time;
			if( time_diff > rte_get_tsc_hz() >> 15 ) {
				J_htb_update_inner();
				last_update_time = rte_rdtsc();
			}
		} else {
			/* Handle packets */
			if( lcore_id == 1 || lcore_id == 2){
				pkt_dequeue_flow();
			}
			current_time = rte_rdtsc();
			time_diff = current_time - last_time;
			if (unlikely(time_diff > TIME_TX_DRAIN)) {
					// Flush tx buffer
				rte_eth_tx_buffer_flush(port_no, lcore_id % NB_TX_QUEUE, tx_buffer[lcore_id % NB_TX_QUEUE]);
				last_time = current_time;
			}
		}
	}
}

/* Parse the argument given in the command line of the application */
static int
parse_args(int argc, char **argv)
{
	port_no = 0;
	return 0;
}

int
main(int argc, char **argv)
{
	uint32_t lcore_id;
	int ret, i;

	/* EAL init */
	ret = rte_eal_init(argc, argv);
	if (ret < 0){
		rte_exit(EXIT_FAILURE, "Invalid EAL parameters\n");
	}
	argc -= ret;
	argv += ret;
	if (rte_lcore_count() != 3) {
		rte_exit(EXIT_FAILURE, "This application does not accept only three cores. "
		"Please adjust the \"-c COREMASK\" parameter accordingly.\n");
	}

	/* Application non-EAL arguments parse */
	ret = parse_args(argc, argv);
	if (ret < 0){
		rte_exit(EXIT_FAILURE, "Invalid input arguments\n");
	}
	/* Buffer pool init */
	pool = rte_pktmbuf_pool_create("pool", NB_MBUF, MEMPOOL_CACHE_SIZE,
		0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
	if (pool == NULL){
		rte_exit(EXIT_FAILURE, "Buffer pool creation error\n");
	}
	/* NIC init */
	/* Donot use the multiple tx queues -- use only one queue? */
	ret = rte_eth_dev_configure(port_no, 1, NB_TX_QUEUE, &port_conf);
	if (ret < 0){
		rte_exit(EXIT_FAILURE, "Port %d configuration error (%d)\n", port_no, ret);
	}
	ret = rte_eth_rx_queue_setup(port_no, 0, NIC_RX_QUEUE_DESC,
					rte_eth_dev_socket_id(port_no),	NULL, pool);
	if (ret < 0){
		rte_exit(EXIT_FAILURE, "Port %d RX queue setup error (%d)\n", port_no, ret);
	}
	for( i=0; i<NB_TX_QUEUE; i++ ){
		ret = rte_eth_tx_queue_setup(port_no, i, NIC_TX_QUEUE_DESC,
						rte_eth_dev_socket_id(port_no),	NULL);
		if (ret < 0){
			rte_exit(EXIT_FAILURE, "Port %d TX queue %d setup error (%d)\n", port_no,
					i, ret);
		}
	}
	char str[20];
	for (int i = 0; i < NB_TX_QUEUE; i++) {
		sprintf(str, "tx_queue_%d", i);
		tx_buffer[i] = rte_zmalloc_socket(str,
				RTE_ETH_TX_BUFFER_SIZE(PKT_TX_BURST_MAX), 0,
				rte_eth_dev_socket_id(port_no));
		if (tx_buffer[i] == NULL) {
			rte_exit(EXIT_FAILURE, "Port %d TX buffer[%d] allocation error\n",
					port_no, i);
		}
		if( rte_eth_tx_buffer_init(tx_buffer[i], PKT_TX_BURST_MAX)){
			rte_exit(EXIT_FAILURE, "Init the %d queue buffer failure\n", i);
		}
	}
	ret = rte_eth_dev_start(port_no);
	if (ret < 0){
			rte_exit(EXIT_FAILURE, "Port %d start error (%d)\n", port_no, ret);
	}
	rte_eth_promiscuous_enable(port_no);
	/* App configuration */
	ret = init_app();
	if (ret < 0){
		rte_exit(EXIT_FAILURE, "Invalid configure flow table\n");
	}
	/* Launch per-lcore init on every lcore */
	rte_eal_mp_remote_launch(main_loop, NULL, CALL_MASTER);
	RTE_LCORE_FOREACH_SLAVE(lcore_id) {
		if (rte_eal_wait_lcore(lcore_id) < 0)
			return -1;
	}

	return 0;
}
