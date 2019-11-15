/* Include Standard library*/
#include <stdio.h>
#include <getopt.h>
#include <unistd.h>
#include <locale.h>
#include <string.h>

/* Include DPDK library */
#include <rte_common.h>
#include <rte_eal.h>
#include <rte_mempool.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_cycles.h>
#include <rte_malloc.h>
#include <rte_meter.h>

/* Include the option header file */
#include "init.h"
/* Include others*/
#include "global_variables.h"

/** Function used for the init.c*/
static int self_args(int argc, char **argv);
static int initialize(void);
static void print_usage(void);
static inline int str_is(const char *str, const char *is);
/** Variables for the setting*/
#define NIC_RX_QUEUE_DESC		128
#define NIC_TX_QUEUE_DESC		512
#define PKT_TX_BURST_MAX		1023*2
#define PKT_RX_BURST_MAX		32
#define TIME_TX_DRAIN                   20000ULL

/* Define Configuration */
static struct rte_eth_conf port_conf = {
		.rxmode = {
				.mq_mode = ETH_MQ_RX_RSS,
				.max_rx_pkt_len = ETHER_MAX_LEN,
				.split_hdr_size = 0,
				.header_split = 0,
				.hw_ip_checksum = 1,
				.hw_vlan_filter = 0,
				.jumbo_frame = 0,
				.hw_strip_crc = 0,
		},
		.rx_adv_conf = {
				.rss_conf = {
						.rss_key = NULL,
						.rss_hf = ETH_RSS_IP,
				},
		},
		.txmode = {
				.mq_mode = ETH_DCB_NONE,
		},
};



int init_parse_args(int argc, char **argv) {
	/* initialize EAL first*/
	int ret = rte_eal_init(argc, argv);
	if (ret < 0) {
		rte_exit(EXIT_FAILURE, "Invalid EAL parameters!\n");
	}
	argc -= ret;
	argv += ret;
	/* initialize self setting*/
	ret = self_args(argc, argv);
	if (ret < 0) {
		rte_exit(EXIT_FAILURE, "Invalid options!\n"
				"Check the parameter after '--' \n"
				"See detailed information with -h\n");
	}

	ret = initialize();
	if (ret < 0) {
		rte_exit(EXIT_FAILURE, "Cannot initialize!\n");
	}
	return 0;
}

// TODO
// Define some parameter usage for option
static int self_args(int argc, char **argv) {
	int opt;
	int option_index;
	const char *optname;
	//char *prgname = argv[0];
	//uint32_t i, nb_lcores;

	static struct option lgopts[] ={
			{ "port", 1, 0, 0 },
			{ "help", 0, 0, 0 },
			{ "others", 1, 0, 0 },
			{ NULL, 0, 0, 0 }
	};

	/* set en_US locale to print big numbers with ',' */
	//setlocale(LC_NUMERIC, "en_US.utf-8");
	const char _require_para[] = "h";
	while ((opt = getopt_long(argc, argv, _require_para, lgopts, &option_index)) != EOF) {

		switch (opt) {
		case 'h':
			print_usage();
			break;
			/* long options */
		case 0:
			optname = lgopts[option_index].name;
			if (str_is(optname, "help")) {
				print_usage();
				break;
			}
			break;

		default:
			print_usage();
			return -1;
		}
	}

	/* check some parameters and set */
	// TODO
	// Set the port number
	port_rx = 1;
	port_tx = 0;

	return 0;
}

// TODO:
// initialize with optimal variables
static int initialize(void) {
	/* Buffer pool init */
	// TODO: rte_pktmbuf_pool_create para meaning, number of elements in the mbuf pool, Cache_size's meaning?
	pool = rte_pktmbuf_pool_create("pool", 8192 - 1, 256, 0,
			RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
	if (pool == NULL) {
		rte_exit(EXIT_FAILURE, "Buffer pool creation error\n");
	}
	/* NIC init */
	int ret = rte_eth_dev_configure(port_rx, 1, 1, &port_conf);
	if (ret < 0) {
		rte_exit(EXIT_FAILURE, "Port %d configuration error (%d)\n", port_rx, ret);
	}
	ret = rte_eth_rx_queue_setup(port_rx, 0, NIC_RX_QUEUE_DESC,
			rte_eth_dev_socket_id(port_rx), NULL, pool);
	if (ret < 0) {
		rte_exit(EXIT_FAILURE, "Port %d RX queue setup error (%d)\n", port_rx, ret);
	}

	ret = rte_eth_dev_configure(port_tx, 1, 1, &port_conf);
	if (ret < 0) {
		rte_exit(EXIT_FAILURE, "Port %d configuration error (%d)\n", port_tx, ret);
	}

	ret = rte_eth_tx_queue_setup(port_tx, 0, NIC_TX_QUEUE_DESC,
			rte_eth_dev_socket_id(port_tx), NULL);
	if (ret < 0) {
		rte_exit(EXIT_FAILURE, "Port %d TX queue 0 setup error (%d)\n", port_tx, ret);
	}
	int NB_TX_QUEUE = 2;
	char str[20];
	for (int i = 0; i < NB_TX_QUEUE; i++ ) {
			sprintf(str, "tx_queue_%d", i);
			printf("111\n");
			tx_buffer[i] = rte_zmalloc_socket(str, RTE_ETH_TX_BUFFER_SIZE(PKT_TX_BURST_MAX), 0, rte_eth_dev_socket_id(port_tx));
			if (tx_buffer[i] == NULL)
				rte_exit(EXIT_FAILURE, "Port %d TX buffer[%d] allocation error\n", port_tx, i);
			rte_eth_tx_buffer_init(tx_buffer[i], PKT_TX_BURST_MAX);
		}
	ret = rte_eth_dev_start(port_rx);
	if (ret < 0) {
		rte_exit(EXIT_FAILURE, "Port %d start error (%d)\n", port_rx, ret);
	}
	ret = rte_eth_dev_start(port_tx);
	if (ret < 0) {
		rte_exit(EXIT_FAILURE, "Port %d start error (%d)\n", port_tx, ret);
	}
	rte_eth_promiscuous_enable(port_rx);
	rte_eth_promiscuous_enable(port_tx);
	return 0;
}

// TODO: list the usage function
static const char usage[] =
		        "                                                                               \n"
				"  EAL-PARAMS -- APP-PARAMS                                                     \n"
				"APP-PARAMS:\n"
				"    -h, --help:    Show the help document                                     \n"
				"    --rsz \"A, B, C\" :   Ring sizes                                           \n"
				"           A = Size (in number of buffer descriptors) of each of the NIC RX    \n"
				"               rings read by the I/O RX lcores (default value is [what])        \n"
				"           B = Size (in number of elements) of each of the SW rings used by the\n"
				"               I/O RX lcores to send packets to worker lcores (default value is\n"
				"           C = Size (in number of buffer descriptors) of each of the NIC TX    \n"
				"               rings written by worker lcores (default value is [what])         \n";
static void print_usage(void) {
	printf(usage);
}

static inline int str_is(const char *str, const char *is)
{
	return strcmp(str, is) == 0;
}
