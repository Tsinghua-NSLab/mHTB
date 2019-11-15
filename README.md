# TODO
1. 使用gcc 优化编译

2. 把原来的meter改为scheduler调度；使用轮询(轮询增加权重调度标记)+bitmap标记，作为新的设计——目前不使用该设计在专利中！

3. 比较DPDK的scheduler和自己的设计结果！自己实现的所谓的HTB应该丢弃

4. 使用RSS代替原来的Core分类


0406:
自己目前对于HTB的理解出现了偏差
HTB应该分为两部分：
一部分是，速率标记--这部分使用Coral；
另一部分是scheduler--这部分原来的HTB设计和前一部分绑定在了一起
	这部分参考Intel的
	
0406--华为内部的资料:
使用5%的带宽用于1:1还原网络情况——保留包头信息，但是把数据内部压缩；网络内部节点处理时把网包分类，然后使用令牌桶限速进行出包处理；
这样可以使用5%的带宽测试出网络情况，可以进行预约传送； 甚至实现0拥塞和0丢包

	
Arrangement:
[x] Define HTB config structure ( e.g. Linux)
[x] HTB run for one core
[x] HTB config for every core
[x] Simple for the run -- look at the main.c

# Files
| directory | content |
| --------- | ------- |
| origin_demo/ | The main program |
| test_res/ | The record of the test |
| direct_test/ | use the TBF in DPDK source |
TODO: we can change the TBF by self-defined source

# Usage

## main
cd src/
main.h & main.c

In the directory, run:
> make clean && make
> sudo -E ./build/DPDK_demo -c 0xf -n 4 

## pktgen
Use the **main.lua** file to generate packets and record the result

copy main.lua into *~/dpdk/pktgen-3.4.2*, run:
> sudo -E ./app/build/pktgen -c 0xff -n 4 -- -P -T -m "[1:2].0, [3:4].1" -f ./main.lua

# Result

## test the pktsend
show in ./test_res/

## test the update
Because the design needs to update the supply rate, so it is important to test how long it is.
The test result in Ubuntu is about 1M?
But the result in openSUSE is about 100k cycles? if we use the int64, not much change--about 73k.
In windows, about 500k cycles?
Is the origin test false?
