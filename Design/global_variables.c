/*
 * global_variables.c
 *
 *  Created on: Dec 24, 2017
 *      Author: jake
 */
#include <stdio.h>
#include <rte_ethdev.h>
#include "global_variables.h"
/* Define Global variables */
uint8_t port_rx, port_tx;
struct rte_mempool *pool;
struct rte_eth_dev_tx_buffer **tx_buffer;
uint8_t NUM_CORES;
