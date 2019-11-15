#ifndef HTB_H
#define HTB_H
/***********************************************************
 * Define the flow information, the structure is similar with HTB
 * But it is only with two layers
 ***********************************************************/
#define APP_FLOWS_MAX  1024
#define APP_PARENT_MAX 32

#include "flow.h"
// TODO: More complex structure
// Add the vector/others to show the
// Add the bitmap to show the leaf's state: active, borrow/lend bandwidth

struct flow_node{
	uint16_t no;
	struct flow_struct flow;
	uint64_t min;
	uint64_t max;
	uint64_t supply;
};

struct inner_node{
	uint64_t min;
	uint64_t max;
	uint64_t supply;
};

struct flow_node flow_leaf[APP_FLOWS_MAX];
struct inner_node flow_level_1[APP_PARENT_MAX];
struct inner_node root;
// Total rate is 14*1000*1000;

static inline void
J_htb_inline_set_flow_supply(struct flow_node *leaf, uint64_t supply){
	leaf->supply = supply;
	J_flow_set_cir(&(leaf->flow), leaf->supply);
}

static inline void
htb_set_leaf( uint16_t leaf_no,uint64_t rate,uint64_t ceil,uint64_t supply ){
	struct flow_node *leaf = &(flow_leaf[leaf_no]);
	leaf->min = rate;
	leaf->max = ceil;
	J_htb_inline_set_flow_supply(leaf, supply);
}

static inline void
htb_set_parent( uint16_t no, uint64_t rate, uint64_t ceil, uint64_t supply ){
	flow_level_1[no].max = ceil;
	flow_level_1[no].min = rate;
	flow_level_1[no].supply = supply;
}

static inline void
htb_set_root( uint64_t total){
	root.max = root.min = root.supply = total;
}

#define REMAINS (0)

static void htb_update_bandwidth(){
	uint64_t total_supply = root.max;
	uint64_t demand_rate[APP_FLOWS_MAX];
	uint64_t parent_demand[APP_PARENT_MAX];
	uint64_t total_demand;
	// Get the demand rate
	for( uint8_t i = 0; i<APP_PARENT_MAX; i++){
		// TODO: memset to speed up?
		parent_demand[i] = 0;
	}
	uint64_t flow_rate;
	for(uint32_t i =0 ; i<APP_FLOWS_MAX; i++){
		flow_rate = flow_leaf[i].flow.sensor.rate_now.cnt;
		demand_rate[i] =(flow_rate < flow_leaf[i].max)? (flow_rate) : (flow_leaf[i].max);
		if( demand_rate[i] == 0 ){
			demand_rate[i] = 10;
		}
		parent_demand[i % APP_PARENT_MAX] += demand_rate[i];
	}
	total_demand = 0;
	for( uint32_t i = 0; i< APP_PARENT_MAX; i++ ){
		if( parent_demand[i] > flow_level_1[i].max ){
			parent_demand[i] = flow_level_1[i].max;
		}
		total_demand += parent_demand[i];
	}
	// printf("Finish demand calculate\n");
	if( total_demand < total_supply ){
		for(uint32_t i =0 ; i<APP_FLOWS_MAX; i++){
			J_htb_inline_set_flow_supply( &(flow_leaf[i]), demand_rate[i]+REMAINS );
		}
		return;
	}else {
		// TODO: calculation process can be better
		uint64_t over_demand = 0;
		uint64_t parent_supply[APP_PARENT_MAX];
		uint64_t parent_over[APP_PARENT_MAX];
		uint64_t supply_rate[APP_FLOWS_MAX];
		for(uint32_t i = 0; i< APP_PARENT_MAX; i++){
			parent_supply[i] = ( parent_demand[i] <= flow_level_1[i].min )?
					(parent_demand[i]):(flow_level_1[i].min);
			parent_demand[i] -= parent_supply[i];
		}
		for(uint32_t i = 0; i< APP_PARENT_MAX; i++){
			total_supply -= parent_supply[i];
			over_demand += parent_demand[i];
		}

		for(uint32_t i = 0; i< APP_PARENT_MAX; i++){
			if( over_demand != 0 ){
				parent_supply[i] += ( parent_demand[i] * total_supply )
					/ over_demand;
			}
		}

		// update the flow
		for(uint32_t i =0 ; i<APP_FLOWS_MAX; i++){
			supply_rate[i] = ( demand_rate[i] <= flow_leaf[i].min )?
					(demand_rate[i]):(flow_leaf[i].min);
			demand_rate[i] -= supply_rate[i];
			parent_over[i] = 0;
		}
		uint32_t parent_id;
		for(uint32_t i =0 ; i<APP_FLOWS_MAX; i++){
			parent_id = i % APP_PARENT_MAX;
			parent_supply[parent_id] -= supply_rate[i];
			parent_over[parent_id] += demand_rate[i];
		}
		for(uint32_t i =0 ; i<APP_FLOWS_MAX; i++){
			parent_id = i % APP_PARENT_MAX;
			if( parent_over[parent_id] != 0 ){
				supply_rate[i] += ( demand_rate[i] * parent_supply[parent_id])
					/ parent_over[parent_id];
			}
			J_htb_inline_set_flow_supply( &(flow_leaf[i]), supply_rate[i]);
		}
	}
//	J_htb_inline_set_flow_supply(&(flow_child[1]), 1000*3000);
}

static inline int J_htb_init(){
	uint16_t i = 0;
	for (i = 0; i < APP_FLOWS_MAX; i++) {
		J_flow_init(&(flow_leaf[i].flow), NULL);
	}
	for( uint16_t i = 0; i < APP_FLOWS_MAX; i++ ){
		htb_set_leaf( i, 1000*1, 1000*10, 1000*2 );
	}
	return 0;
}

#define APP_PKT_FLOW_POS                33
#define APP_PKT_COLOR_POS               5

#if APP_PKT_FLOW_POS > 64 || APP_PKT_COLOR_POS > 64
#error Byte offset needs to be less than 64
#endif

static inline uint16_t pkt_classify( struct rte_mbuf *pkt){
	uint8_t *pkt_data = rte_pktmbuf_mtod(pkt, uint8_t *);
	uint32_t dst_ip = IPv4(
			(uint32_t)pkt_data[APP_PKT_FLOW_POS-3],
			(uint32_t)pkt_data[APP_PKT_FLOW_POS-2],
			(uint32_t)pkt_data[APP_PKT_FLOW_POS-1],
			(uint32_t)pkt_data[APP_PKT_FLOW_POS] );
	return (uint16_t)(dst_ip % (APP_FLOWS_MAX));
}

static inline int
J_htb_enqueue( struct rte_mbuf *pkt ){
	uint16_t flow_id = pkt_classify(pkt);
	uint64_t req_token_num = rte_pktmbuf_pkt_len(pkt) - sizeof(struct ether_hdr);
	req_token_num = 1;
	return J_flow_enqueue( &(flow_leaf[flow_id].flow), pkt, req_token_num );
}

static inline int
J_htb_dequeue( uint8_t lcore_id ){
	// dequeue one packet
}

static inline void print_flow_info( struct flow_node* flow_n){
	struct flow_struct * flow = &(flow_n->flow);
	double supply_rate = ((double)rte_get_tsc_hz()) / (double)(flow->tbf.cir_period);
	supply_rate *= (double)(flow->tbf.cir_tokens_per_period);
	uint64_t supply_rate_res = (uint64_t) supply_rate;
	printf( "req: %8ld\t", flow->token_num_req);
	printf( "dem: %8ld\t"
			"sup: %8ld\n",
			flow->sensor.rate_now.cnt,
			supply_rate_res);
	printf( "min: %8d\t"
			"max: %8d\t"
			"sup: %8ld\n",
			flow_n->min,
			flow_n->max,
			flow_n->supply);
	printf(	"gre: %8ld\t"
			"yel: %8ld\t"
			"red: %8ld\n",
			flow->token_num_out[J_METER_GREEN],
			flow->token_num_out[J_METER_YELLOW],
			flow->token_num_out[J_METER_RED]);

}

#endif
