/*
 * self_red.c
 *
 *  Created on: Dec 19, 2017
 *      Author: jake
 */

// TODO
// make the red for one core
/**
 * 	printf("Core %u: port RX = %d, port TX = %d\n", lcore_id, port_rx, port_tx);
	// Use one core to transfer
	uint32_t count_rx = 0, count_tx = 0;
	rte_red_mark_queue_empty(&red_structure, last_time);
	while (1) {
		uint64_t time_diff;
		unsigned i, nb_rx;

		// Mechanism to avoid stale packets in the output buffer
		current_time = rte_rdtsc();
		time_diff = current_time - last_time;
		if (unlikely(time_diff > TIME_TX_DRAIN)) {
			// Flush tx buffer, send the packets
			if( tx_buffer->length > 0){
				printf("Send %d packets when time reaches\n",tx_buffer->length);
				int success_no = rte_eth_tx_buffer_flush(port_tx, 0, tx_buffer);
				count_tx += success_no;
				printf("Success: %d\n",success_no);
				printf("--------------------------------- \n");
			}
			rte_red_mark_queue_empty(&red_structure,last_time);
			last_time = current_time;
		}

		// Read packet burst from NIC RX
		nb_rx = rte_eth_rx_burst(port_rx, 0, pkts_rx, PKT_RX_BURST_MAX);
		if( nb_rx <= 0 ){
			continue;
		}
		unsigned res_len = tx_buffer->length + nb_rx;
		printf("Next No:%d\n",res_len);
		print_red_default();
		int res_enqueue = rte_red_enqueue(&red_config, &red_structure, res_len ,current_time );
		printf("res_enqueue result:%d\n",res_enqueue);
		print_red_default();
		// Handle packets
		for (i = 0; i < nb_rx; i++) {
			count_rx ++;
			if( ((count_rx) & 0x1) == 1 ){
				printf("There is %d packet received\n", count_rx );
			}
			struct rte_mbuf *pkt = pkts_rx[i];
			if( res_enqueue == 0 ){
				rte_eth_tx_buffer(port_tx, 0, tx_buffer, pkt);
			}else if (res_enqueue == 1){
				rte_pktmbuf_free(pkt); //1 drop the packet based on max threshold criteria
			}else{
				rte_pktmbuf_free(pkt); //2 drop the packet based on mark probability criteria
			}
		}
	}
 */
