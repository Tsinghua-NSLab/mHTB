/*
 * self_red.h
 *
 *  Created on: Dec 19, 2017
 *      Author: jake
 */

#ifndef SELF_RED_H_
#define SELF_RED_H_
#include <rte_red.h>

static struct rte_red red_structure = {
		.avg = 0,      /**< Average queue size (avg), scaled in fixed-point format */
		.count = 0,    /**< Number of packets since last marked packet (count) */
		.q_time = 0   /**< Start of the queue idle time (q_time) */
};

static struct rte_red_config red_config = {
		.min_th = 32 << RTE_RED_SCALING,   /**< min_th scaled in fixed-point format */
		.max_th = RTE_RED_MAX_TH_MAX << RTE_RED_SCALING,   /**< max_th scaled in fixed-point format */
		.pa_const = (4*99166) << RTE_RED_SCALING  , /**< Precomputed constant value used for pa calculation (scaled in fixed-point format) */
		// 2 * (max_th - min_th) * maxp_inv
		.maxp_inv = 2,  /**< maxp_inv */
		.wq_log2 = 2  /**< wq_log2 */
};


void print_red(const struct rte_red *red_struc ){
	printf("red_structure, avg: %d,count: %d,q_time: %ld\n",
			red_struc->avg,
			red_struc->count,
			red_struc->q_time);

}

#endif /* SELF_RED_H_ */
