/*
 * DPDK_debug.h
 *
 *  Created on: Dec 24, 2017
 *      Author: jake
 */

#ifndef DPDK_DEBUG_H_
#define DPDK_DEBUG_H_

/* debugging support; S is subsystem, these are defined:
  0 - netlink messages
  1 - enqueue
  2 - drop & requeue
  3 - dequeue main
  4 - dequeue one prio DRR part
  5 - dequeue class accounting
  6 - class overlimit status computation
  7 - hint tree
  8 - event queue
 10 - rate estimator
 11 - classifier
 12 - fast dequeue cache

 L is level; 0 = none, 1 = basic info, 2 = detailed, 3 = full
 q->debug uint32 contains 16 2-bit fields one for subsystem starting
 from LSB
 */
// Donot Define Debug self, use the rte_debug
//#define DPDK_DEBUG
//#ifdef DPDK_DEBUG
//#define DPDK_DBG_COND(S,L) (1)
//#define DPDK_DBG_OUT(FMT,ARG...) printf(FMT,##ARG)
//#else
//#define DPDK_DBG_COND(S,L) (0)
//#define DPDK_DBG_OUT(S,L,FMT,ARG...)
//#endif




#endif /* DPDK_DEBUG_H_ */
