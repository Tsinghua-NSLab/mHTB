#ifndef HTB_H
#define HTB_H
/***********************************************************
 * Define the flow information, the structure is similar with HTB
 * But it is only with two layers
 *
 *
 ***********************************************************/
#define APP_FLOWS_MAX  4
#define APP_PARENT_MAX 2

#include <stdio.h>
#include "flow.h"
// TODO: More complex structure
// Add the vector/others to show the
// Add the bitmap to show the leaf's state: active, borrow/lend bandwidth
struct flow_node{
	uint8_t no;
	struct flow_info flow_node_info;
	struct flow_node *parent;
};

struct flow_node flow_child[APP_FLOWS_MAX];
struct flow_node parent[APP_PARENT_MAX];
struct flow_node root;

static inline void
J_htb_inline_set_flow(struct flow_node *node, uint64_t rate,uint64_t ceil, uint64_t max_token){
	struct trTCM_param tr_param;
	tr_param.param_min.token_per_second = rate;
	tr_param.param_max.token_per_second = ceil;
	tr_param.param_min.max_token_number = max_token;
	tr_param.param_max.max_token_number = max_token;
	J_flow_init( &(node->flow_node_info), &tr_param );
}

static inline int J_htb_init(){
	uint16_t i = 0;
	for(i = 0; i < APP_FLOWS_MAX; i++) {
		J_htb_inline_set_flow(&(flow_child[i]), 20*1000, 60*1000, 10);
		flow_child[i].parent = &(parent[i % APP_PARENT_MAX]);
	}
	for(i = 0; i < APP_PARENT_MAX; i++ ){
		J_htb_inline_set_flow(&(parent[i]), 40*1000,80*1000,10);
		parent[i].parent = &root;
	}
	J_htb_inline_set_flow(&root, 100*1000, 100*1000, 10);
	root.parent = NULL;
	return 0;
}

static inline enum meter_color
J_htb_req_token(uint16_t flow_id, uint64_t token_num){
	enum meter_color color = J_METER_NO_COLOR;
	struct flow_node *p = &(flow_child[flow_id]);
	while( p != NULL ){
		color = J_flow_req_token(&(p->flow_node_info), token_num);
		if( color == J_METER_RED ){
			// the packet is larger than the ceil
			return J_METER_RED;
		}
		p = p->parent;
	}
	if( color == J_METER_RED ){
		return J_METER_YELLOW;
	}else{
		return J_METER_GREEN;
	}
}

static inline void print_flow_info( struct flow_info* flow){
	double supply_min = ((double)rte_get_tsc_hz()) / (double)(flow->tr.rate_min.cycles_per_update);
	supply_min *= (double)(flow->tr.rate_min.tokens_per_update);
	double supply_max = ((double)rte_get_tsc_hz()) / (double)(flow->tr.rate_max.cycles_per_update);
	supply_max *= (double)(flow->tr.rate_max.tokens_per_update);
	printf( "req: %8ld \t", flow->token_num_req);
	printf( "dem: %8ld \t"
			"min: %8ld \t"
			"max: %8ld\n",
			flow->sensor.rate_now.cnt,
			(uint64_t) supply_min,
			(uint64_t) supply_max);

	printf(	"gre: %8ld \t"
			"yel: %8ld \t"
			"red: %8ld\n",
			flow->token_num_out[J_METER_GREEN],
			flow->token_num_out[J_METER_YELLOW],
			flow->token_num_out[J_METER_RED]);
}

static inline void print_htb_leaf_info(uint16_t flow_num){
	printf("Child Flow: %d\n",flow_num);
	print_flow_info(&(flow_child[flow_num].flow_node_info));
}

static inline void print_htb_inner_info(uint16_t flow_num){
	printf("Parent Flow: %d\n",flow_num);
	print_flow_info(&(parent[flow_num].flow_node_info));
}

static inline void print_htb_root_info(){
	printf("Root\n");
	print_flow_info(&(root.flow_node_info));
}


#endif
