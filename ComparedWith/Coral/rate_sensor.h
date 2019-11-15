#ifndef RATE_SENSOR_H
#define RATE_SENSOR_H

/*****************************************************************
 * The sensor for the rate of token request
 *****************************************************************/
struct J_pkt_cnt {
//	uint64_t byte;	// count the length
	uint32_t cnt; 	// count number
};
// count the rate
struct J_pkt_rate {
	struct J_pkt_cnt rate_now;
	struct {
		// update the rate with the following parameters
		struct J_pkt_cnt max_update_num;
		// if packets reach the number/ bytes, update the rate
		uint64_t max_cycle_interval;
		// if the interval reaches the limit, update

		struct J_pkt_cnt curr_pkt_cnt; 	
		// packet count
		uint64_t last_cycle_num;	 	
		// record the last time, use the cycle number
	} update_parm;
};

static inline void J_inline_clear_pkt_cnt(struct J_pkt_cnt* pkt_cnt_node){
    pkt_cnt_node->cnt = 0;
}

static inline void J_inline_clear_pkt_rate(struct J_pkt_rate *pkt_rate_node) {
	J_inline_clear_pkt_cnt(&(pkt_rate_node->rate_now));
	J_inline_clear_pkt_cnt(&(pkt_rate_node->update_parm.curr_pkt_cnt));
	pkt_rate_node->update_parm.last_cycle_num = rte_rdtsc();
}

static inline void J_rs_init_pkt_rate(struct J_pkt_rate *pkt_rate_node) {
	J_inline_clear_pkt_rate(pkt_rate_node);
	// set the update parameter
	pkt_rate_node->update_parm.max_cycle_interval = rte_get_tsc_hz() >> 2;
	// about 250ms, update the sensor result!
	pkt_rate_node->update_parm.max_update_num.cnt = 100;
	// get 1000 packets, update the sensor result!
}


/**
 * Update the rate of token number consumption
 *
 * @param rate
 *   The rate record
 * @param token_num
 *   At present, ask for the number of token
 */
static inline void J_rate_sensor_update_rate(struct J_pkt_rate *rate, uint64_t token_num) {
	struct J_pkt_cnt *curr_pkt_info = &(rate->update_parm.curr_pkt_cnt);
	curr_pkt_info->cnt += token_num;
	uint64_t current_time = rte_get_tsc_cycles();
	uint64_t cycle_interval = current_time - rate->update_parm.last_cycle_num;
	// struct J_pkt_cnt *max_pkt_info = &(rate->update_parm.max_update_num);
	struct J_pkt_cnt *pkt_rate_record = &(rate->rate_now);
	if( cycle_interval < rte_get_tsc_hz() ){
		/**
		 * Update rate in a narrow time -- less than 1s
		 */
		if (cycle_interval >= rate->update_parm.max_cycle_interval ){
				//|| curr_pkt_info->cnt >= max_pkt_info->cnt) {
			// TODO: How to sensor the flow rate
			// TODO: speed up with '>>' instead of '/'
			pkt_rate_record->cnt = ((uint64_t)curr_pkt_info->cnt
					* rte_get_tsc_hz()) / cycle_interval; // per second packets
			rate->update_parm.last_cycle_num = current_time;
			J_inline_clear_pkt_cnt(curr_pkt_info);
		}
	}else{
		// if the interval is very large, the sensor is not accurate
		// just use the large result as the res
		// Maybe set the single: not active too much
		pkt_rate_record->cnt = curr_pkt_info->cnt; // per second packets as the time
		rate->update_parm.last_cycle_num = current_time;
		J_inline_clear_pkt_cnt(curr_pkt_info);
	}
}

#endif
