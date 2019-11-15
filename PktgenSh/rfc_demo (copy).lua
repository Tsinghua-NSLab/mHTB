-- RFC-2544 throughput testing.
--
package.path = package.path ..";?.lua;test/?.lua;app/?.lua;../?.lua"

require "Pktgen";

-- 这里定义了我们要发送的包的大小，这里可以自己随便定义。
local pktSizes		= { 64, 128, 256, 512, 1024, 1280, 1518 };
local firstDelay	= 3;
local delayTime		= 30;		-- Time in seconds to wait for tx to stop
local pauseTime		= 1;
local sendport		= "0";
local recvport		= "1";
-- 发包机接收端的IP
local dstip			= "192.168.1.1";
-- 发包机发送端的IP
local srcip			= "192.168.0.1";
local netmask		= "/24";
local pktCnt		= 4000000;
-- 虚拟机接收端的mac地址
local mac               = "00:00:00:00:00:01"

local foundRate;

pktgen.set_mac(sendport, mac);
pktgen.set_ipaddr(sendport, "dst", dstip);
pktgen.set_ipaddr(sendport, "src", srcip..netmask);

pktgen.set_ipaddr(recvport, "dst", srcip);
pktgen.set_ipaddr(recvport, "src", dstip..netmask);

pktgen.set_proto(sendport..","..recvport, "udp");

--关闭pktgen的实时显示
pktgen.screen("off");


local function doWait(port)
	local stat, idx, diff;

	-- Try to wait for the total number of packets to be sent.
	local idx = 0;
	while( idx < (delayTime - firstDelay) ) do
		stat = pktgen.portStats(port, "port")[tonumber(port)];

		diff = stat.ipackets - pktCnt;
		print(idx.." ipackets "..stat.ipackets.." delta "..diff);

		idx = idx + 1;
		if ( diff == 0 ) then
			break;
		end

		local sending = pktgen.isSending(sendport);
		if ( sending[tonumber(sendport)] == "n" ) then
			break;
		end
		pktgen.delay(pauseTime * 1000);
	end

end

-- 开始进行测试的函数
local function testRate(size, rate)
	local stat, diff;

	pktgen.set(sendport, "rate", rate);
	pktgen.set(sendport, "size", size);

	pktgen.clr();
	pktgen.delay(500);

	pktgen.start(sendport);
	pktgen.delay(firstDelay * 1000);

	doWait(recvport);

	pktgen.stop(sendport);

	pktgen.delay(pauseTime * 1000);

	stat = pktgen.portStats(recvport, "port")[tonumber(recvport)];
	diff = stat.ipackets - pktCnt;
	--printf(" delta %10d", diff);

	return diff;
end

local function GetPreciseDecimal(nNum, n)
    if type(nNum) ~= "number" then
        return nNum;
    end
    n = n or 0;
    n = math.floor(n)
    if n < 0 then
        n = 0;
    end
    local nDecimal = 1/(10 ^ n)
    if nDecimal == 1 then
        nDecimal = nNum;
    end
    local nLeft = nNum % nDecimal;
    return nNum - nLeft;
end
local function midpoint(imin, imax)
	return (imin + ((imax - imin) / 2));
end

local function doSearch(size, minRate, maxRate)
	local diff, midRate;

	if ( maxRate < minRate ) then
		return 0.0;
	end

    -- 查找本次需要发送的速率
	midRate = midpoint(minRate, maxRate);

	--printf("    Testing Packet size %4d at %3.0f%% rate", size, midRate);
	--printf(" (%f, %f, %f)\n", minRate, midRate, maxRate);
    
    -- 对带宽进行测试
	diff = testRate(size, midRate);

    -- 这里允许配置如果 最大速率和最小速率相差 0.0001 就直接退出。
    if (maxRate - minRate < 0.0001) then
            return foundRate;
    end

	if ( diff < 0 ) then
		printf("\n");
		return doSearch(size, minRate, midRate);
	elseif ( diff > 0 ) then
		printf("\n");
		return doSearch(size, midRate, maxRate);
	else
		if ( midRate > foundRate ) then
			foundRate = midRate;
		end
		if ( (foundRate == 100.0) or (foundRate == 1.0) ) then
			return foundRate;
		end
		if ( (minRate == midRate) and (midRate == maxRate) ) then
			return foundRate;
		end
               
		return doSearch(size, midRate, maxRate);
	end
end

function main()
	local size;

	pktgen.clr();

	pktgen.set(sendport, "count", pktCnt);

	print("\nRFC2544 testing... (Not working Completely) ");

    -- 根据配置的包，对不同包大小进行测试，并输出最后结果
	for _,size in pairs(pktSizes) do
		foundRate = 0.0;
		printf("    >>> %d Max Rate %3.0f%%\n", size, doSearch(size, 1.0, 100.0));
	end
end

main();

