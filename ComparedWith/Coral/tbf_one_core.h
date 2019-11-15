#ifndef TBF_ONE_CORE_H
#define TBF_ONE_CORE_H

/*****************************************************************
 * The file defines the Token Bucket Filter(TBF) for a single core
 *
 * The output is used as the Single Rate Three Color Maker(srTCM) in RFC2697
 * The TBF is about the cir, cbs and ces.
 * Key Function:
 * 		+------------------------------+-------------------------------+
 * 		| J_tbf_one_core_set_config    | Configure a TBF               |
 * 		| J_tbf_one_core_req_tbf_token | srTCM result for token number |
 * 		+------------------------------+-------------------------------+
 *****************************************************************/
#include <math.h>
#include <rte_cycles.h>
struct tbf_one_core{
	uint64_t time; /* Time of latest update of C and E token buckets */
	uint64_t tc;   /* Number of bytes currently available in the committed (C) token bucket */
	uint64_t te;   /* Number of bytes currently available in the excess (E) token bucket */
	uint64_t cbs;  /* Upper limit for C token bucket */
	uint64_t ebs;  /* Upper limit for E token bucket */
	uint64_t cir_period; /* Number of CPU cycles for one update of C and E token buckets */
	uint64_t cir_tokens_per_period; /* Number of bytes to add to C and E token buckets on each update */
};

struct tbf_one_core_param {
	uint64_t cir; /**< Committed Information Rate (CIR). Measured in bytes per second. */
	uint64_t cbs; /**< Committed Burst Size (CBS).  Measured in bytes. */
	uint64_t ebs; /**< Excess Burst Size (EBS).  Measured in bytes. */
};

enum meter_color{
	J_METER_GREEN = 0, /**< Green */
	J_METER_YELLOW,    /**< Yellow */
	J_METER_RED,       /**< Red */
	J_METER_COLORS     /**< Number of available colors */
};


#define METER_TB_PERIOD_MIN      100

static inline void
J_inline_set_params(uint64_t hz, uint64_t rate, uint64_t *tb_period, uint64_t *tb_bytes_per_period)
{
	double period = ((double) hz) / ((double) rate);

	if (period >= METER_TB_PERIOD_MIN) {
		*tb_bytes_per_period = 1;
		*tb_period = (uint64_t) period;
	} else {
		*tb_bytes_per_period = (uint64_t) ceil(METER_TB_PERIOD_MIN / period);
		*tb_period = (hz * (*tb_bytes_per_period)) / rate;
	}
}


/**
 * Configure the TBF with some parameters: CIR, CBS, EBS
 *
 * @param m
 *   The TBF
 * @param params
 *   The configuration of the TBF
 * @return
 *   0 upon success, error code otherwise
 */
static inline int
J_tbf_one_core_set_config(struct tbf_one_core *m, struct tbf_one_core_param *params)
{
	uint64_t hz;
	/* Check input parameters */
	if ((m == NULL) || (params == NULL)) {
		return -1;
	}

	if ((params->cir == 0) || ((params->cbs == 0) && (params->ebs == 0))) {
		return -2;
	}

	/* Initialize srTCM run-time structure */
	hz = rte_get_tsc_hz();
	m->time = rte_get_tsc_cycles();
	m->tc = m->cbs = params->cbs;
	m->te = m->ebs = params->ebs;
	J_inline_set_params(hz, params->cir, &m->cir_period, &m->cir_tokens_per_period);
	return 0;
}

static inline int
J_tbf_one_core_set_cir(struct tbf_one_core *m, uint64_t cir)
{
	uint64_t hz;
	hz = rte_get_tsc_hz();
	J_inline_set_params(hz, cir, &m->cir_period, &m->cir_tokens_per_period);
	return 0;
}

static inline enum meter_color
J_inline_req_token(struct tbf_one_core *m, uint32_t req_token_num){
	if ( m->tc >= req_token_num) {
			m->tc -= req_token_num;
			return J_METER_GREEN;
	}else if ( m->te >= req_token_num) {
		// TODO: m->tc + m->te OR m->te is the yellow?
			m->te -= req_token_num;
			return J_METER_YELLOW;
	}else{
		return J_METER_RED;
	}
}

static inline void
J_inline_update_bucket(struct tbf_one_core *m){
	uint64_t time_diff, n_periods;
	/* Bucket update */
	time_diff = rte_get_tsc_cycles() - m->time;
	// It may cause a bug, if the time is less than the m->time
	n_periods = time_diff / m->cir_period;
	m->time += n_periods * m->cir_period;

	/* Put the tokens overflowing from tc into te bucket */
	m->tc += n_periods * m->cir_tokens_per_period;
	if (m->tc > m->cbs) {
		m->te += (m->tc - m->cbs);
		if (m->te > m->ebs){
			m->te = m->ebs;
		}
			m->tc = m->cbs;
	}
}

/**
 * Request the token and  the TBF with some parameters: CIR, CBS, EBS
 *
 * @param m
 *   The TBF
 * @param req_token_num
 *   The number of request token number
 * @return
 *   meter_color according the rule of srTCM
 */
static inline enum meter_color
J_tbf_one_core_req_tbf_token(struct tbf_one_core *m, uint32_t req_token_num){
	if( m->tc >= req_token_num ){
		m->tc -= req_token_num;
		return J_METER_GREEN;
	}else{
		// TODO: Do a just before request, if the bucket is large, will it be useful?
		J_inline_update_bucket(m);
		return J_inline_req_token(m,req_token_num);
	}
}
#endif
