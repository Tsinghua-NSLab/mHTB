/*
 * main.h
 *
 *  Created on: Jan 4, 2018
 *      Author: jake
 */

#ifndef MAIN_H_
#define MAIN_H_
/**
 * Define some types
 * pkt_cnt, pkt_rate, tbf, flow
 */
// count the packet
struct pkt_cnt {
	uint64_t byte;	// count the length
	uint32_t cnt; 	// count number
};


// count the rate
struct pkt_rate {
	struct pkt_cnt rate_now;
	struct {
		// update the rate with the following parameters
		struct pkt_cnt max_update_num; 	// if packets reach the number/ bytes, update the rate
		uint64_t max_cycle_interval;	// if the interval reaches the limit, update

		struct pkt_cnt curr_pkt_cnt; 	// packet count
		uint64_t last_cycle_num;	 	// record the last time, use the cycle number
	} update_parm;
};

// tbf structure
struct token_bucket_filter {
	uint64_t curr_token_num; 		//available token number
	uint64_t ctn_max; 				// committed token number upper limit
	uint32_t token_rate; 			// token increase rate per second
	uint64_t last_cycle_num; 		// last time of the token update, use the cycle number
	uint64_t max_cycle_interval; 	// max update time interval
	struct rte_ring *queue; 		// a queue for the flow
};

// flow structure
struct flow_class {
#ifdef DEMO_FLOW_COUNT
	// statistic of the packets in the node
	struct pkt_cnt pkt_receive;
	struct pkt_cnt pkt_send;
	struct pkt_cnt pkt_drop;
#endif
	struct pkt_rate pkt_receive_rate;
	/** TODO:
	 * the tbf can be changed into a pointer
	 * so it will support different kinds of queues
	 * Change the token_bucket_filter into other kinds
	 */
	struct token_bucket_filter tbf;
	uint16_t queue_id;
	struct rte_eth_dev_tx_buffer *buffer;
	/** drop the rb tree structure
	 * each flow is just a flow and has no other relationship with
	 * and donot need other information
	 */
};


/**
 * The debug information
 */
#define DPDK_DBG_ENABLE

#ifdef DPDK_DBG_ENABLE
#define DPDK_DBG_OUT(FMT,ARG...) printf(FMT,##ARG)
#else
#define DPDK_DBG_OUT(FMT,ARG...)
#endif


#endif /* MAIN_H_ */
