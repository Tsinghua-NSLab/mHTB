/*
 * global_variables.h
 *
 *  Created on: Dec 19, 2017
 *      Author: jake
 */

// !! include this file as the last to avoid the missing definition

#ifndef GLOBAL_VARIABLES_H_
#define GLOBAL_VARIABLES_H_
// #include <rte_ethdev.h>
/* Declare Global variables */
extern uint8_t port_rx, port_tx;
extern struct rte_mempool *pool;
extern struct rte_eth_dev_tx_buffer  *tx_buffer;
extern uint8_t NUM_CORES;

#endif /* GLOBAL_VARIABLES_H_ */
