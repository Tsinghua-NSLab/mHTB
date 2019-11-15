#ifndef TBF_MULTI_CORES_H
#define TBF_MULTI_CORES_H

/*****************************************************************
 * The file defines the Token Bucket Filter(TBF) for multiple cores
 *
 * The output is used as the Single Rate Three Color Maker(srTCM) in RFC2697
 * The TBF is about the cir, cbs and ces.
 * Key Function:
 * 		+------------------------------+-------------------------------+
 * 		| J_tbf_set_config    | Configure a TBF               |
 * 		| J_tbf_req_tbf_token | srTCM result for token number |
 * 		+------------------------------+-------------------------------+
 * Assuming: different cores have the same HZ
 *****************************************************************/
#include <math.h>
#include <stdio.h>
#include <rte_cycles.h>
#include <rte_spinlock.h>
#include <rte_lcore.h>

#define MAX_MULTI_CORE_NUM  3
#define METER_TB_PERIOD_MIN      100.0
/*
 * Define one TBF
 */
struct tbf{
	// TODO: is it better to use time instead of token as in Linux kernel
	uint64_t hz;
	uint64_t last_update_cycle;
	// TODO: is it better to use the uint32_t instead of the uint64_t
	uint64_t token_number;
	uint64_t max_token_number;
	uint64_t cycles_per_update;
	uint64_t tokens_per_update;
};

struct tbf_param {
	uint64_t token_per_second; /**< Committed Information Rate (CIR). Measured in bytes per second. */
	uint64_t max_token_number; /**< Committed Burst Size (CBS).  Measured in bytes. */
};

/**
 * Configure the TBF with parameters
 *
 * @param m
 *   The TBF
 * @param params
 *   The configuration of the TBF
 * @return
 *   0 upon success, error code otherwise
 */
static inline void
J_tbf_set_config(struct tbf *m, struct tbf_param *params)
{
	/*TODO: Check input parameters */

	/* Initialize trTCM run-time structure */
	m->hz = rte_get_tsc_hz(); // record the frequency
	m->token_number = m->max_token_number = params->max_token_number;
	m->last_update_cycle = rte_get_tsc_cycles();
	double period = ((double) m->hz) / ((double) params->token_per_second);
	if (period >= METER_TB_PERIOD_MIN) {
		m->tokens_per_update = 1;
		m->cycles_per_update = (uint64_t) period;
	} else {
		m->tokens_per_update = (uint64_t) ceil(METER_TB_PERIOD_MIN / period);
		m->cycles_per_update = (m->hz * (m->tokens_per_update)) / (params->token_per_second);
	}
	/*printf("Hz: %ld, token_number: %ld,"
			"Tokens: %ld, Cycles: %ld\n",
			m->hz,m->token_number,
			m->tokens_per_update, m->cycles_per_update);*/
}

static inline void
J_inline_update_bucket(struct tbf *m){
	uint64_t n_update;
	uint64_t cycle_diff = rte_get_tsc_cycles();
	if( cycle_diff > (m->last_update_cycle) ){
		// If multiple cores request, the max CPU cycle is the criterion
		cycle_diff -= m->last_update_cycle;
		n_update = cycle_diff / (m->cycles_per_update);
		uint64_t cycle_add = n_update * (m->cycles_per_update);
		m->last_update_cycle += cycle_add;
		m->token_number += n_update * (m->tokens_per_update );
		if ( (m->token_number) > (m->max_token_number) ){
			(m->token_number) = (m->max_token_number);
		}
	}
}

static inline uint8_t
J_tbf_req_token(struct tbf *m, uint64_t req_token_num){
	J_inline_update_bucket(m);
	if ( m->token_number >= req_token_num) {
		m->token_number -= req_token_num;
		return 1;
	}else{
		return 0;
	}
}

/*
 * Define HTB's TBF: trTCM
 */
struct trTCM{
	// two rate three color meter
	struct tbf rate_min; // min bandwidth
	struct tbf rate_max; // max bandwidth
	rte_spinlock_t tr_lock;
};

struct trTCM_param{
	struct tbf_param param_min;
	struct tbf_param param_max;
};

enum meter_color{
	J_METER_GREEN = 0, /**< Green, min < rate */
	J_METER_YELLOW,    /**< Yellow, min < rate < max */
	J_METER_RED,       /**< Red, rate > max*/
	J_METER_COLORS,     /**< Number of available colors */
	J_METER_NO_COLOR
};


static inline void
J_trTCM_set_config(struct trTCM *m, struct trTCM_param *params)
{
	/*TODO: Check input parameters */

	/* Initialize trTCM run-time structure */
	//printf("set the config\n");
	J_tbf_set_config(&(m->rate_min), &(params->param_min));
	J_tbf_set_config(&(m->rate_max),&(params->param_max));
	rte_spinlock_init(&(m->tr_lock));
}

/**
 * Request the token
 *
 * @param m
 *   The TBF
 * @param req_token_num
 *   The number of request token number
 * @return
 *   meter_color according the rule of srTCM
 */
static inline enum meter_color
J_trTCM_req_token(struct trTCM *m, uint64_t req_token_num){
	enum meter_color res = J_METER_GREEN;
	// TODO: change the order as DPDK -- first just the Max then the min
	rte_spinlock_lock(&(m->tr_lock));
	if( J_tbf_req_token(&(m->rate_min),req_token_num) ){
		J_tbf_req_token(&(m->rate_max),req_token_num);
	}else if( J_tbf_req_token(&(m->rate_max),req_token_num) ){
		res = J_METER_YELLOW;
	}else{
		res = J_METER_RED;
	}
	rte_spinlock_unlock(&(m->tr_lock));
	return res;
}

#endif
