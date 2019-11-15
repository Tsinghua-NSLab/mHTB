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
#include <rte_cycles.h>
#include <rte_spinlock.h>
#include <rte_lcore.h>

#define MAX_MULTI_CORE_NUM  3
#define METER_TB_PERIOD_MIN      100



struct tbf_one_core{
/**
 * To solve the multiple core's time synchronous -- multiple cores have the same hz
 * We assume the accurate real time as t, the {CPU i}'s timestamp cycle as t(i)
 * We should find T(i) to make that, for all i, t(i)-T(i) = t
 * init_time is the T(i)
 * CPU i 's last visited time is last_time = t(i)-T(i)
 * last_update_time = max{t(i)-T(i)}
 *
 * To reduce the calculation, we use the time to replace the token
 *
 */
	uint64_t init_time[MAX_MULTI_CORE_NUM];
	// the timestamp record for t=0 for each core:
	uint64_t last_time[MAX_MULTI_CORE_NUM];
	uint64_t last_update_time;
	uint64_t hz;
	uint64_t tc;   /* Number of bytes currently available in the committed (C) token bucket */
	uint64_t te;   /* Number of bytes currently available in the excess (E) token bucket */
	uint64_t cbs;  /* Upper limit for C token bucket */
	uint64_t ebs;  /* Upper limit for E token bucket */
	uint64_t cir_period; /* Number of CPU cycles for one update of C and E token buckets */
	uint64_t cir_tokens_per_period; /* Number of tokens to add to C and E token buckets on each update */
	uint64_t pir_period; /* Number of CPU cycles for one update of P token bucket */
	uint64_t pir_bytes_per_period; /* Number of bytes to add to P token bucket on each update */
	rte_spinlock_t tbf_lock;
	uint32_t check_time; // record the time for the synchronous
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
J_tbf_set_config(struct tbf_one_core *m, struct tbf_one_core_param *params)
{
	/* Check input parameters */
	if ((m == NULL) || (params == NULL)) {
		return -1;
	}

	if ((params->cir == 0) || ((params->cbs == 0) && (params->ebs == 0))) {
		return -2;
	}

	/* Initialize srTCM run-time structure */
	m->hz = rte_get_tsc_hz(); // record the frequency
	m->tc = m->cbs = params->cbs;
	m->te = m->ebs = params->ebs;
	rte_spinlock_init(&(m->tbf_lock));// init the spinlock
	for( uint8_t i = 0; i < MAX_MULTI_CORE_NUM; i++){
		m->init_time[i] = 0;
		m->last_time[i] = 0;
	}
	m->last_update_time = 0;
	m->check_time = 0;
	//TODO: Will it be fast if use memset()?

	J_inline_set_params(m->hz, params->cir, &m->cir_period, &m->cir_tokens_per_period);
	return 0;
}

static inline int
J_tbf_set_cir(struct tbf_one_core *m, uint64_t cir)
{
	J_inline_set_params(m->hz, cir, &m->cir_period, &m->cir_tokens_per_period);
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
	uint64_t* init_tsc = &(m->init_time[rte_lcore_id()]);
	uint64_t* last_tsc = &(m->last_update_time[rte_lcore_id()]);
	if( (*init_tsc) == 0 ){
		// The first time for the CPU to visit
		*last_tsc = rte_get_tsc_cycles();
		*init_tsc = *last_tsc - m->last_update_time;
		m->check_time++;
	}else{
		// TODO: According to how large the bandwidth is, it may be better to add
		// 		a judgment whether the time_diff is larger than xxxx
		time_diff = rte_get_tsc_cycles() - (*last_tsc);
		if( time_diff > m->last_update_time ){
			time_diff -= m->last_update_time;
			n_periods = time_diff / m->cir_period;
			uint64_t time_add = n_periods * m->cir_period;
			m->last_update_time += time_add;
			*last_tsc += time_add;
			/* Put the tokens overflowing from tc into te bucket */
			m->tc += n_periods * m->cir_tokens_per_period;
			if (m->tc > m->cbs) {
				m->te += (m->tc - m->cbs);
				if (m->te > m->ebs){
					m->te = m->ebs;
				}
				m->tc = m->cbs;
			}
		}else{
			*last_tsc = rte_get_tsc_cycles();
			*init_tsc = *last_tsc - m->last_update_time;
			m->check_time++;
		}
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
J_tbf_req_tbf_token(struct tbf_one_core *m, uint32_t req_token_num){

#define TBF_LOCK rte_spinlock_lock(&(m->tbf_lock))
#define TBF_UNLOCK rte_spinlock_unlock(&(m->tbf_lock))
	enum meter_color res = J_METER_GREEN;
	TBF_LOCK;
	if( m->tc >= req_token_num ){
		m->tc -= req_token_num;
	}else{
		// TODO: Do a just before request, if the bucket is large, will it be useful?
		J_inline_update_bucket(m);
		res = J_inline_req_token(m,req_token_num);
	}
	TBF_UNLOCK;
	return res;
#undef TBF_LOCK
#undef TBF_UNLOCK

}

#undef METER_TB_PERIOD_MIN
#undef MAX_MULTI_CORE_NUM
#endif
