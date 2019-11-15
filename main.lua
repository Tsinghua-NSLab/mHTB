package.path = package.path ..";?.lua;test/?.lua;app/?.lua;../?.lua"

require "Pktgen";

-- some function
-- function: tonumber("1") = 1
-- prints("portStats", pktgen.portStats("all", "port"));
-- 这里定义了我们要发送的包的信息，这里可以自己随便定义。
-- 关于如何配置包和ip等,参看http://pktgen-dpdk.readthedocs.io/en/latest/lua.html
local pktSizes		= { 64, 128, 256, 512, 1024, 1280, 1518 };
local firstDelay	= 3;
local delayTime		= 30;		
local pauseTime		= 1;
local sendport		= "0";
local recvport		= "1";
local dstip		= "192.168.1.1";
local srcip		= "192.168.0.1";
local netmask		= "/24";
local pktCnt		= 4000000;
local mac               = "00:00:00:00:00:01"
local foundRate;

-- 设置包的信息
pktgen.set_mac(sendport, mac);

pktgen.set_ipaddr(sendport, "dst", dstip);
pktgen.set_ipaddr(sendport, "src", srcip..netmask);

pktgen.set_ipaddr(recvport, "dst", srcip);
pktgen.set_ipaddr(recvport, "src", dstip..netmask);

pktgen.set_proto(sendport..","..recvport, "udp"); -- '..' is used for strcat 

pktgen.screen("off");

-- 设置rate和size
pktgen.set(sendport, "rate", 100); -- full speed
pktgen.set(sendport, "size", 64); -- 包最小是64
pktgen.clr();

pktgen.delay(500);

-- 开始发包
pktgen.start(sendport);

-- 等待稳定(单位ms)
-- pktgen.delay(firstDelay * 1000);

--local stat;
--stat = pktgen.portStats("1", "port")[1]["ipackets"];
--pktgen.portStats("all", "rate");
--printf("port receive packets, %d\n", stat);
local num = 0;

while(1)
do

--[[ printf("Out time each line to find when it is x.0 s\n");
for i=1,10,1
do
	printf("%s\n",os.date("%X", os.time())); -- get present time
	pktgen.delay(100); --find when is the x.0 s
end
printf("\n");
--]]

printf("Time/100ms, Rec rate/pps, Rec rate/Mbps, Send rate/pps, Send rate/Mbps\n");
pktgen.delay(1000);
	while(num<15) 
	do
		pktgen.delay(1000);
		local stat_pps_1 = pktgen.portStats("1", "rate")[1].pkts_rx;
		local stat_mbits_1 = pktgen.portStats("1", "rate")[1].mbits_rx;
		local stat_pps_0 = pktgen.portStats("0", "rate")[0].pkts_tx;
		local stat_mbits_0 = pktgen.portStats("0", "rate")[0].mbits_tx;

		printf("%d,%d,%d,%d,%d\n",num, stat_pps_1, stat_mbits_1, stat_pps_0, stat_mbits_0);
		num = num + 1;
	end

	while(1) 
	do
		io.write("End? y/n:") ;
		local judge = io.read(1);
		io.write(judge.."\n");
		if(judge == "y") then
			do
				return;
			end
		elseif(judge == "n") then
			break;
		else
			printf("input wrong!\n");
		end
	end
num = 0;

end
--pktgen.screen("on");


