#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
//Windows
#ifdef _WIN32

#include <intrin.h>
uint64_t rdtsc(){
	return __rdtsc();
}
// Linux/GCC
#else

uint64_t rdtsc(){
	unsigned int lo,hi;
	__asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
	return ((uint64_t)hi<< 32) | lo;
}

#endif

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define NUM_per_VM	32
#define NUM_VM		32

// bps
#define MAX_RATE	100*1024*1024
#define MID_RATE	20*1024*1024
#define MIN_RATE	512*1024

typedef uint64_t Rate_unit;
Rate_unit supply_rate[NUM_VM][NUM_per_VM];
Rate_unit demand_rate[NUM_VM][NUM_per_VM];
Rate_unit max_rate[NUM_VM][NUM_per_VM];
Rate_unit min_rate[NUM_VM][NUM_per_VM];
Rate_unit VM_max_rate[NUM_VM];
Rate_unit rate_total;

void update_double(){
    Rate_unit sum_demand = 0;
    Rate_unit temp_demand[NUM_VM];
    int vm_no;
    int port_no;
    for( vm_no = 0; vm_no < NUM_VM; vm_no++){
        temp_demand[vm_no] = 0;
        for( port_no = 0; port_no < NUM_per_VM; port_no++){
            temp_demand[vm_no] += demand_rate[vm_no][port_no];
        }
        sum_demand += temp_demand[vm_no];
    }
    // update supply rate
    for( vm_no = 0; vm_no < NUM_VM; vm_no++){
        for( port_no = 0; port_no < NUM_per_VM; port_no++){
            double demand = (double)demand_rate[vm_no][port_no];
            Rate_unit temp_min = MIN( demand/((double)temp_demand[vm_no])*VM_max_rate[vm_no] , demand/((double)sum_demand)*rate_total );
            supply_rate[vm_no][port_no] = MAX(MIN(temp_min, max_rate[vm_no][port_no]),min_rate[vm_no][port_no]);
        }
    }
}

void update_int(){
    Rate_unit sum_demand = 0;
    Rate_unit temp_demand[NUM_VM];
    int vm_no;
    int port_no;
    for( vm_no = 0; vm_no < NUM_VM; vm_no++){
        temp_demand[vm_no] = 0;
        for( port_no = 0; port_no < NUM_per_VM; port_no++){
            temp_demand[vm_no] += demand_rate[vm_no][port_no];
        }
        sum_demand += temp_demand[vm_no];
    }
    // update supply rate
    for( vm_no = 0; vm_no < NUM_VM; vm_no++){
        for( port_no = 0; port_no < NUM_per_VM; port_no++){
            Rate_unit demand = demand_rate[vm_no][port_no];
            Rate_unit _temp1 = (demand*VM_max_rate[vm_no])/(temp_demand[vm_no]);
            int move = 1;
            Rate_unit _temp2 = (demand*(rate_total>>move))/((sum_demand>>move));
            Rate_unit temp_min = MIN( _temp1 , _temp2 );
            supply_rate[vm_no][port_no] = MAX(MIN(temp_min, max_rate[vm_no][port_no]),min_rate[vm_no][port_no]);
        }
    }
}

int init_state(){
// initial the state
    rate_total = 0;
    int vm_no;
    int port_no;
    for( vm_no = 0; vm_no < NUM_VM; vm_no++){
        VM_max_rate[vm_no] = 0;
        for( port_no = 0; port_no < NUM_per_VM; port_no ++ ){
            min_rate[vm_no][port_no] = MIN_RATE + (MID_RATE-MIN_RATE)*(((10.0*rand())/(RAND_MAX+1.0))/10.0);
            max_rate[vm_no][port_no] = MID_RATE + (MAX_RATE-MID_RATE)*(((10.0*rand())/(RAND_MAX+1.0))/10.0);
            VM_max_rate[vm_no] += max_rate[vm_no][port_no];
            //printf("%f,%f\n",min_rate[vm_no][port_no],max_rate[vm_no][port_no]);
        }
        rate_total += VM_max_rate[vm_no];
        VM_max_rate[vm_no] *= 0.4 + ((((10.0*rand())/(RAND_MAX+1.0))/10.0))*0.6;
    }
    rate_total /= 3;
    return 0;
}

int rand_demand(){
    int vm_no;
    int port_no;
    for( vm_no = 0; vm_no < NUM_VM; vm_no++){
        for( port_no = 0; port_no < NUM_per_VM; port_no++){
            demand_rate[vm_no][port_no] = rand();
        }
    }
    return 0;
}

int main(){
    srand((int)time(0));
    printf("Origin Double:%ld\n",rdtsc());
    init_state();
    rand_demand();
    int no;
    for( no = 0; no < 10; no++){
        uint64_t start_cycle = rdtsc();
        update_double();
        uint64_t end_cycle = rdtsc();
        printf("%ld\t",end_cycle-start_cycle);
    }
    printf("\nOrigin int64:%ld\n",rdtsc());
    for( no = 0; no < 10; no++){
        uint64_t start_cycle = rdtsc();
        update_int();
        uint64_t end_cycle = rdtsc();
        printf("%ld\t",end_cycle-start_cycle);
    }
    printf("\n");
    
    
    return 0;
}





