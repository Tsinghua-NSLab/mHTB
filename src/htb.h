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

#include "flow.h"
// TODO: More complex structure
// Add the vector/others to show the
// Add the bitmap to show the leaf's state: active, borrow/lend bandwidth
struct flow_leaf{
	uint8_t no;
	struct flow_info tbf_info;
	uint64_t min;
	uint64_t max;
	uint64_t supply;
};


struct flow_leaf flow_child[APP_FLOWS_MAX];
uint64_t parent_shared[APP_PARENT_MAX];
uint64_t root_shared;
uint64_t rate_total;
// Total rate is 14*1000*1000;

static struct flow_leaf* J_htb_leaf( uint16_t flow_no){
	return &(flow_child[flow_no]);
}

static inline void
J_htb_inline_set_flow_supply(struct flow_leaf *leaf, uint64_t supply){
	leaf->supply = supply;
	J_flow_set_cir(&(leaf->tbf_info), leaf->supply);
}

static inline void
J_htb_inline_set_flow(struct flow_leaf *leaf, uint64_t rate,uint64_t ceil, uint64_t supply){
	leaf->min = rate;
	leaf->max = ceil;
	J_htb_inline_set_flow_supply(leaf, supply);
}

static inline void
htb_set_leaf( uint16_t leaf_no,uint64_t rate,uint64_t ceil,uint64_t supply ){
	J_htb_inline_set_flow(&flow_child[leaf_no], rate, ceil, supply);
}

static inline void
htb_set_parent( uint16_t parent_no, uint64_t rate, uint64_t ceil, uint64_t supply ){
	parent_shared[parent_no] = supply;
}

static inline void
htb_set_root( uint64_t total){
	rate_total = total;
	for( unsigned i=0; i<APP_FLOWS_MAX;i++){
		total -= flow_child[i].supply;
	}
	for( unsigned i =0; i<APP_PARENT_MAX; i++ ){
		total -= parent_shared[i];
	}
	if( total > 0 ){
		root_shared = total;
	}else{
		printf("ERROR!\n");
	}
}

#define REMAINS (100)

static void htb_update_bandwidth(){
	uint64_t total = rate_total;
	uint64_t demand_rate[APP_FLOWS_MAX];
	uint64_t parent_demand[APP_PARENT_MAX];
	uint64_t total_demand;
	// Get the demand rate
	for(uint8_t i =0 ; i<APP_FLOWS_MAX; i++){
		uint64_t flow_rate = flow_child[i].tbf_info.sensor.rate_now.cnt;
		demand_rate[i] =(flow_rate < flow_child[i].max)? (flow_rate) : (flow_child[i].max);
	}
	parent_demand[0] = demand_rate[0] + demand_rate[1];
	parent_demand[1] = demand_rate[2] + demand_rate[3];
	total_demand = parent_demand[0]+ parent_demand[1];
	if( total_demand < total ){
		for(uint8_t i =0 ; i<APP_FLOWS_MAX; i++){
			J_htb_inline_set_flow_supply( &(flow_child[i]), demand_rate[i] );
		}
		return;
	}else {
		uint64_t upper_demand = 0;
		for(uint8_t i =0 ; i<APP_FLOWS_MAX; i++){
			if( demand_rate[i] < flow_child[i].min ){
				J_htb_inline_set_flow_supply( &(flow_child[i]), demand_rate[i] );
				demand_rate[i] = 0;
			}else{
				J_htb_inline_set_flow_supply( &(flow_child[i]), flow_child[i].min );
				demand_rate[i] -= flow_child[i].min;
				upper_demand += demand_rate[i];
			}
		}
		uint64_t upper_supply = total - (total_demand - upper_demand);
		for(uint8_t i =0 ; i<APP_FLOWS_MAX; i++){
			if( demand_rate[i] > 0 ){
				uint64_t new_supply = (double)demand_rate[i] * (double)upper_supply / (double)upper_demand;
				J_htb_inline_set_flow_supply( &(flow_child[i]), new_supply);
				demand_rate[i] = 0;
			}
		}
	}
//	J_htb_inline_set_flow_supply(&(flow_child[1]), 1000*3000);
}

/*static void htb_update_part(uint16_t flow_no){

}*/

static inline int J_htb_init(){
	uint16_t i = 0;
	for (i = 0; i < APP_FLOWS_MAX; i++) {
		J_flow_init(&(flow_child[i].tbf_info), NULL);
	}
#include "tree_struct.inc"
	return 0;
}

static inline enum meter_color
J_htb_req_token(uint16_t flow_id, uint32_t token_num){
	// if( flow_id < APP_FLOWS_MAX){
	// Maybe Need check the flow_id, if it is larger than flow_leaf number?
		return J_flow_req_flow_token(&(flow_child[flow_id].tbf_info), token_num);
}

static inline void print_flow_info( struct flow_info* flow){
	double supply_rate = ((double)rte_get_tsc_hz()) / (double)(flow->tbf.cir_period);
	supply_rate *= (double)(flow->tbf.cir_tokens_per_period);
	uint64_t supply_rate_res = (uint64_t) supply_rate;
	printf( "req: % 8ld \t", flow->token_num_req);
	printf( "dem: % 8ld \t"
			"sup: % 8ld\n",
			flow->sensor.rate_now.cnt,
			supply_rate_res);

	printf(	"gre: % 8ld \t"
			"yel: % 8ld \t"
			"red: % 8ld\n",
			flow->token_num_out[J_METER_GREEN],
			flow->token_num_out[J_METER_YELLOW],
			flow->token_num_out[J_METER_RED]);

}

static inline void print_htb_leaf_info(uint16_t flow_num){
	printf("Child Flow: %d\n",flow_num);
	print_flow_info(&(flow_child[flow_num].tbf_info));
	//printf("%ld \t %ld \t %ld\n",flow->tbf.cir_period, flow->tbf.cir_bytes_per_period, rte_get_tsc_hz());
}

static inline void print_htb_inner_info(uint16_t flow_num){
	printf("Parent Flow: %d\n",flow_num);
	printf("Remain: %ld\n", parent_shared[flow_num]);
}

static inline void print_htb_root_info(){
	printf("Root\n");
	printf("Remain: %ld\n", root_shared);
}


#endif
