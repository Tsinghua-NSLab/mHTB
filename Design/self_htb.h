/*
 * self_htb.h
 *
 *  Created on: Dec 19, 2017
 *      Author: jake
 *  Usage:
 *  	Define the HTB algorithm for one core
 */

#ifndef SELF_HTB_H_
#define SELF_HTB_H_

// TODO:Use the rte_log and rte_debug to debug
// TODO:It is better to use hash table instead of tree, but we can use tree to try first
//The class of HTB, leaf or inner node
struct htb_class{
	uint32_t classid;
	struct rate_est{
		uint32_t pkt_rate;
		uint64_t byte_rate;
	};
};

struct htb_structure{
	struct htb_pkt_record{
		uint64_t num_pkt_in;
		uint64_t num_pkt_out;
		uint64_t num_pkt_drop;
	};
};


struct htb_class
{
    /* general class parameters */
    u32 classid;
    struct gnet_stats_basic bstats;
    struct qstats {
    	__u32	qlen;
    	__u32	backlog;
    	__u32	drops;
    	__u32	requeues;
    	__u32	overlimits;
    };
    struct xstats{
    	__u32 lends;
    	__u32 borrows;
    	__u32 giants;	/* too big packets (rate will not be accurate) */
    	__u32 tokens;
    	__u32 ctokens;
    };
    int refcnt;			/* usage count of this class */
    /* rate measurement counters */
    unsigned long rate_bytes,sum_bytes;
    unsigned long rate_packets,sum_packets;

    /* topology */
    int level;			/* our level (see above) */
    struct htb_class *parent;	/* parent class */
    struct list_head hlist;	/* classid hash list item */
    struct list_head sibling;	/* sibling list item */
    struct list_head children;	/* children list */

    union {
	    struct htb_class_leaf {
		    struct Qdisc *q;
		    int prio;
		    int aprio;
		    int quantum;
		    int deficit[TC_HTB_MAXDEPTH];
		    struct list_head drop_list;
	    } leaf;
	    struct htb_class_inner {
		    struct rb_root feed[TC_HTB_NUMPRIO]; /* feed trees */
		    struct rb_node *ptr[TC_HTB_NUMPRIO]; /* current class ptr */
            /* When class changes from state 1->2 and disconnects from
               parent's feed then we lost ptr value and start from the
              first child again. Here we store classid of the
              last valid ptr (used when ptr is NULL). */
              u32 last_ptr_id[TC_HTB_NUMPRIO];
	    } inner;
    } un;
    struct rb_node node[TC_HTB_NUMPRIO]; /* node for self or feed tree */
    struct rb_node pq_node;		 /* node for event queue */
    unsigned long pq_key;	/* the same type as jiffies global */

    int prio_activity;		/* for which prios are we active */
    enum htb_cmode cmode;	/* current mode of the class */

    /* class attached filters */
    struct tcf_proto *filter_list;
    int filter_cnt;

    int warned;		/* only one warning about non work conserving .. */

    /* token bucket parameters */
    struct qdisc_rate_table *rate;	/* rate table of the class itself */
    struct qdisc_rate_table *ceil;	/* ceiling rate (limits borrows too) */
    long buffer,cbuffer;		/* token bucket depth/rate */
    long mbuffer;			/* max wait time */
    long tokens,ctokens;		/* current number of tokens */
    psched_time_t t_c;			/* checkpoint time */
};

struct htb_config{

};

struct htb_structure* htb_init( struct htb_config *HTB_config );
struct rte_mbuf* htb_enqueue( struct htb_structure *htb_qdisc, struct rte_mbuf *pkt );
struct rte_mbuf* htb_dequeue( struct htb_structure *htb_qdisc );

#endif /* SELF_HTB_H_ */
