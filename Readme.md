# Usage

## DPDK_demo
main.h & main.c

In the directory, run:
> make clean && make
> sudo -E ./build/DPDK_demo -c 0xf -n 4 

## Pktgen
Use the **main.lua** file to generate packets and record the result

copy main.lua into *~/dpdk/pktgen-3.4.2*, run:
> sudo -E ./app/build/pktgen -c 0xff -n 4 -- -P -T -m "[1:2].0, [3:4].1" -f ./main.lua

# Record_result

show in ./Test_Result/
