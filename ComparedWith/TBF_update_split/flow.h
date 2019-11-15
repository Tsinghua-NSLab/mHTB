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

enum noder_type{
	Inner_node,
	Leaf_node
};

struct flow_info{
	union{
		struct trTCM_leaf tr_leaf;
		struct trTCM_inner tr_inner;
	}un;
	enum noder_type type;
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
	enum meter_color output_color;
	if( flow->type == Inner_node ){
		output_color = J_trTCM_inner_req_token(&(flow->un.tr_inner), token_num );
	}else if( flow->type == Leaf_node ){
		output_color = J_trTCM_leaf_req_token(&(flow->un.tr_leaf), token_num );
	}
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
J_flow_init(struct flow_info* flow, struct trTCM_param* param, enum noder_type type){
	// Because the DPDK send packets is a burst, so the CBS should be large
	// init the TBF structure with large CBS and EBS
	if(param == NULL){
		param = &app_srtcm_params;
	}
	flow->type = type;
	if( type == Inner_node ){
		J_trTCM_inner_set_config(&(flow->un.tr_inner), param);
	}else if( type == Leaf_node){
		J_trTCM_leaf_set_config(&(flow->un.tr_leaf), param);
	}
	J_rs_init_pkt_rate(&(flow->sensor));
	// clear the statistics
	flow->token_num_req = 0;
	for( uint8_t i = 0; i < J_METER_COLORS; i++ ){
		flow->token_num_out[i] = 0;
	}
}

#endif
