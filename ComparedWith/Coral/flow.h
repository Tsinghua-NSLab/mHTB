#ifndef FLOW_H
#define FLOW_H
/***********************************************************************
 * A flow is formed by the following:
 *  1. The sensor of its rate
 *  2. The TBF to do metering
 *  3. The statistics of the flow
 * Question TODO:
 *  How to sensor the rate?
 *  	In the design, it occurs when the packet requests the tokens
 *  	But it may be inaccurate, if the design is moved into the scheduler
 ***********************************************************************/
#include "rate_sensor.h"
#include "tbf_one_core.h"

struct flow_struct{
	struct tbf_one_core tbf;
	struct J_pkt_rate sensor;
	uint64_t token_num_req;
	uint64_t token_num_out[J_METER_COLORS];
	struct rte_ring *queue;
	uint16_t buffer_id;
	struct rte_eth_dev_tx_buffer *buffer;
};

static inline int
J_flow_enqueue(struct flow_struct* flow, struct rte_mbuf *pkt, uint32_t token_num){
	J_rate_sensor_update_rate(&(flow->sensor), token_num);
	return rte_ring_enqueue(flow->queue, (void *) pkt);
}

static inline enum meter_color
J_flow_req_flow_token(struct flow_struct* flow, uint32_t token_num){
	/* color input is not used for blind modes */
	flow->token_num_req += token_num;
	J_rate_sensor_update_rate(&(flow->sensor), token_num); //  calculate the demand rate
	enum meter_color output_color = J_tbf_one_core_req_tbf_token(&(flow->tbf), token_num);
	flow->token_num_out[output_color] += token_num;
	return output_color;
}

static inline int
J_flow_dequeue( struct flow_struct* flow ){
	struct rte_mbuf *pkt;
	if (rte_ring_dequeue(flow->queue, (void *) &pkt) == 0) {
		uint64_t req_token_num = rte_pktmbuf_pkt_len(pkt) - sizeof(struct ether_hdr);
		req_token_num = 1;
		int action_res = J_flow_req_flow_token( flow, req_token_num);
		if ( action_res == J_METER_GREEN ){
			l2fwd_mac_updating(pkt, port_no, &l2fwd_src_mac);
			rte_eth_tx_buffer(port_no, flow->buffer_id, flow->buffer, pkt);
			return 0;
		} else {
			rte_ring_enqueue(flow->queue, (void *)pkt);
			return 1;
		}
	}else{
		return -1;
	}
}

#define DEFAULT_CIR (1000 * 100)
static struct tbf_one_core_param app_srtcm_params = {
		.cir = DEFAULT_CIR,
		.cbs = 10,
		.ebs = 10//pps
};

static uint16_t rte_ring_num = 0;
#define RING_SIZE     	 			1024 // Define the default ring_size

static inline int
J_flow_init(struct flow_struct* flow, struct tbf_one_core_param* param,
		uint16_t buffer_id, struct rte_eth_dev_tx_buffer *buffer_des ){
	// Because the DPDK send packets is a burst, so the CBS should be large
	// init the TBF structure with large CBS and EBS
	if(param == NULL){
		param = &app_srtcm_params;
	}
	int ret = J_tbf_one_core_set_config(&(flow->tbf), param);
	if (ret){
		return ret;
	}
	// init the sensor
	J_rs_init_pkt_rate(&(flow->sensor));
	// clear the statistics
	flow->token_num_req = 0;
	for( uint8_t i = 0; i < J_METER_COLORS; i++ ){
		flow->token_num_out[i] = 0;
	}
	flow->buffer_id = buffer_id;
	flow->buffer = buffer_des;
	{
		char str[20];
		sprintf(str, "ring_%d", rte_ring_num++);
		flow->queue = rte_ring_create(str, RING_SIZE, SOCKET_ID_ANY,
				0 ); // multi enqueue and multi dequeue
	}
	return 0;
}

static inline int
J_flow_set_param(struct flow_struct* flow, struct tbf_one_core_param* param){
	return J_tbf_one_core_set_config(&(flow->tbf), param);
}

static inline int
J_flow_set_cir(struct flow_struct* flow, uint64_t cir){
	return J_tbf_one_core_set_cir(&(flow->tbf), cir);
}
#endif
