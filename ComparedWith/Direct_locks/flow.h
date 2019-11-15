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
#include "define_log.h"
#include "rate_sensor.h"
#include "trTCM.h"

struct flow_info{
	struct trTCM tr;
	struct J_pkt_rate sensor;
	uint64_t token_num_req;
	uint64_t token_num_out[J_METER_COLORS];
};

static inline enum meter_color
J_flow_req_token(struct flow_info* flow, uint64_t token_num)
{
	/* color input is not used for blind modes */
	flow->token_num_req += token_num;
	J_rate_sensor_update_rate(&(flow->sensor), token_num); //  calculate the demand rate
	enum meter_color output_color = J_trTCM_req_token(&(flow->tr), token_num);
	flow->token_num_out[output_color] += token_num;
	return output_color;
}

#define DEFAULT_CIR (1000 * 100)
static struct trTCM_param app_srtcm_params = {
	.param_min ={
		.token_per_second = DEFAULT_CIR,
		.max_token_number = 10
	},
	.param_max = {
		.token_per_second = DEFAULT_CIR*2,
		.max_token_number = 10
	}
};

static inline void
J_flow_init(struct flow_info* flow, struct trTCM_param* param){
	// Because the DPDK send packets is a burst, so the CBS should be large
	// init the TBF structure with large CBS and EBS
	if(param == NULL){
		param = &app_srtcm_params;
	}
	J_trTCM_set_config(&(flow->tr), param);
	// init the sensor
	J_rs_init_pkt_rate(&(flow->sensor));
	// clear the statistics
	flow->token_num_req = 0;
	for( uint8_t i = 0; i < J_METER_COLORS; i++ ){
		flow->token_num_out[i] = 0;
	}
}

/*static inline int
J_flow_set_param(struct flow_info* flow, struct tbf_one_core_param* param){
	return J_tbf_set_config(&(flow->tbf), param);
}*/

/*
static inline int
J_flow_set_cir(struct flow_info* flow, uint64_t cir){
	return J_tbf_set_cir(&(flow->tbf), cir);
}
*/
#endif
