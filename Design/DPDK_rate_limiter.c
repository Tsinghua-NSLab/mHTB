/*
 * DPDK_rate_limiter.c
 *
 *  Created on: Dec 7, 2017
 *      Author: jake
 */

/* Include Standard library*/
#include <stdio.h>
#include <unistd.h>
/* Include DPDK library */
#include <rte_common.h>
#include <rte_mbuf.h>
#include <rte_meter.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_malloc.h>
#include <rte_mempool.h>
/* Include the option header file */
#include "init.h"
#include "global_variables.h"

#include "DPDK_rate_limiter.h"

/* Define CONST in the file */

#define NIC_RX_QUEUE_DESC		128
#define NIC_TX_QUEUE_DESC		512
#define PKT_TX_BURST_MAX		1023*2
#define PKT_RX_BURST_MAX		32
#define TIME_TX_DRAIN                   20000ULL
#define NIC_RX_QUEUE                    0
#define NIC_TX_QUEUE                    0

#define APP_PKT_FLOW_POS                33
#define APP_PKT_COLOR_POS               5

#define APP_FLOWS_MAX  256

#define NB_SCHED_CORE       2
#define NB_RX_RING          NB_SCHED_CORE
#define NB_TX_QUEUE	        NB_SCHED_CORE

#define NB_QOS_CLASS	    8

#define DPDK_DBG_OUT(FMT,ARG...) printf(FMT,##ARG)

static struct rte_mbuf *pkts_rx[PKT_RX_BURST_MAX];
static struct rte_ring *rx_ring[NB_RX_RING];

enum policer_action {
       ACCEPT = 0,
        DROP = 1,
};

//static struct rte_meter_srtcm qos_class[NB_QOS_CLASS];

static void app_pkt_handle(struct rte_mbuf *pkt, uint32_t ring_id ){
	//uint8_t *pkt_data = rte_pktmbuf_mtod(pkt, uint8_t *);
	uint64_t current_time = rte_rdtsc();
	uint32_t pkt_len = rte_pktmbuf_pkt_len(pkt) - sizeof(struct ether_hdr);
	printf("pkt_len:%d, Time:%ld\n",pkt_len, current_time);
	if(0)
		rte_pktmbuf_free(pkt);
	else
		rte_eth_tx_buffer(port_tx, ring_id, &tx_buffer[ring_id], pkt);
	printf("Success!\n");
	return;

}

static void update_big_token_table(void){
	printf("update!\n");
	return;
}

/**
 * Each core is runing the main_loop
 */
static __attribute__((noreturn)) int main_loop(
		__attribute__((unused)) void *dummy) {
	uint32_t lcore_id = rte_lcore_id();

	DPDK_DBG_OUT("Core %u: port RX = %d, port TX = %d\n", lcore_id, port_rx, port_tx);
	//	TODO: Set the multi-cores work
		if (lcore_id == 0) {
				while (1) {
					int i, nb_rx;
					/* Read packet burst from NIC RX */
					nb_rx = rte_eth_rx_burst(port_rx, NIC_RX_QUEUE, pkts_rx, PKT_RX_BURST_MAX);
					/* Handle packets to different cores */
					for (i = 0; i < nb_rx; i ++) {
						struct rte_mbuf *pkt = pkts_rx[i];
						uint8_t *pkt_data = rte_pktmbuf_mtod(pkt, uint8_t *);
						uint8_t flow_id = (uint8_t)(pkt_data[APP_PKT_FLOW_POS] & (APP_FLOWS_MAX - 1));
						rte_ring_enqueue(rx_ring[flow_id % NB_RX_RING], (void *)pkt);
				}
			}
		}
		else if (lcore_id == 1) {
			printf("Core %u for sched table\n", lcore_id);
			while(1)
			{
				update_big_token_table();
				sleep(5);
			}
		}
		else {
			uint32_t ring_id = lcore_id % NB_RX_RING;
			printf("Core %u for RX_RING[%d]:\n", lcore_id, ring_id);
			while (1) {
				struct rte_mbuf *pkt = NULL;
				if (rte_ring_dequeue(rx_ring[ring_id], (void *) &pkt) == 0) {
					app_pkt_handle(pkt, ring_id);
				}
			}
		}
}



int main(int argc, char **argv) {
	if( init_parse_args(argc,argv) < 0 ){
		rte_exit(EXIT_FAILURE, "Some other faults!");
	}
	char str[20];
	for (int i = 0; i < NB_RX_RING; i++) {
			sprintf(str, "ring_%d", i);
			rx_ring[i] = rte_ring_create(str, 8*1024,
					SOCKET_ID_ANY, RING_F_SP_ENQ | RING_F_SC_DEQ);
	}

	/* Launch per-lcore init on every lcore */
	rte_eal_mp_remote_launch(main_loop, NULL, CALL_MASTER);

	uint32_t lcore_id;
	RTE_LCORE_FOREACH_SLAVE(lcore_id) {
		if (rte_eal_wait_lcore(lcore_id) < 0)
			return -1;
	}



	return 0;
}
