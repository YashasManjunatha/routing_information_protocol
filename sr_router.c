/**********************************************************************
 * file:  sr_router.c
 * date:  Mon Feb 18 12:50:42 PST 2002
 * Contact: casado@stanford.edu
 *
 * Description:
 *
 * This file contains all the functions that interact directly
 * with the routing table, as well as the main entry method
 * for routing.
 *
 **********************************************************************/

#include <stdio.h>
#include <assert.h>


#include "sr_if.h"
#include "sr_rt.h"
#include "sr_router.h"
#include "sr_protocol.h"
#include "sr_arpcache.h"
#include "sr_utils.h"

/*---------------------------------------------------------------------
 * Method: sr_init(void)
 * Scope:  Global
 *
 * Initialize the routing subsystem
 *
 *---------------------------------------------------------------------*/

void sr_init(struct sr_instance* sr)
{
    /* REQUIRES */
    assert(sr);

    /* Initialize cache and cache cleanup thread */
    sr_arpcache_init(&(sr->cache));

    pthread_attr_init(&(sr->attr));
    pthread_attr_setdetachstate(&(sr->attr), PTHREAD_CREATE_JOINABLE);
    pthread_attr_setscope(&(sr->attr), PTHREAD_SCOPE_SYSTEM);
    pthread_attr_setscope(&(sr->attr), PTHREAD_SCOPE_SYSTEM);
    pthread_t arp_thread;

    pthread_create(&arp_thread, &(sr->attr), sr_arpcache_timeout, sr);
    
    srand(time(NULL));
    pthread_mutexattr_init(&(sr->rt_lock_attr));
    pthread_mutexattr_settype(&(sr->rt_lock_attr), PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&(sr->rt_lock), &(sr->rt_lock_attr));

    pthread_attr_init(&(sr->rt_attr));
    pthread_attr_setdetachstate(&(sr->rt_attr), PTHREAD_CREATE_JOINABLE);
    pthread_attr_setscope(&(sr->rt_attr), PTHREAD_SCOPE_SYSTEM);
    pthread_attr_setscope(&(sr->rt_attr), PTHREAD_SCOPE_SYSTEM);
    pthread_t rt_thread;
    pthread_create(&rt_thread, &(sr->rt_attr), sr_rip_timeout, sr);
    /* Add initialization code here! */

} /* -- sr_init -- */

struct sr_if *get_interface_from_ip(struct sr_instance *sr, uint32_t ip_addr) {
	struct sr_if *curr_interface = sr->if_list;
	struct sr_if *dest_interface = NULL;
	while (curr_interface) {
		if (ip_addr == curr_interface->ip) {
			dest_interface = curr_interface;
			break;
		}
		curr_interface = curr_interface->next;
	}
	return dest_interface;
}

void send_icmp_packet(struct sr_instance* sr, uint8_t * packet/* lent */,
		unsigned int len, char* rec_interface/* lent */, uint8_t icmp_type,
		uint8_t icmp_code, struct sr_if *dest_interface) {

	int header_len = sizeof(sr_ip_hdr_t) + sizeof(sr_ethernet_hdr_t)
			+ sizeof(sr_icmp_t3_hdr_t);
	int outgoing_len;
	if (icmp_type == 0x00) {
		outgoing_len = len;
	} else {
		outgoing_len = header_len;
	}

	uint8_t *icmp_send_packet = (uint8_t *) malloc(outgoing_len);
	memset(icmp_send_packet, 0, sizeof(uint8_t) * outgoing_len);

	sr_ethernet_hdr_t *org_eth_hdr = (sr_ethernet_hdr_t *) packet;
	sr_ip_hdr_t *org_ip_hdr = (sr_ip_hdr_t *) (packet
			+ sizeof(sr_ethernet_hdr_t));

	sr_icmp_t3_hdr_t *icmp_send_hdr = (sr_icmp_t3_hdr_t *) (icmp_send_packet
			+ sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t));
	sr_ip_hdr_t *ip_send_hdr = (sr_ip_hdr_t *) (icmp_send_packet
			+ sizeof(sr_ethernet_hdr_t));
	sr_ethernet_hdr_t *eth_send_hdr = (sr_ethernet_hdr_t *) icmp_send_packet;

	struct sr_if *outgoing_interface = sr_get_interface(sr, rec_interface);
	uint32_t source_ip = outgoing_interface->ip;
	if (dest_interface) {
		source_ip = dest_interface->ip;
	}

	if (icmp_type == 0x00) {
		memcpy(icmp_send_hdr,
				(sr_icmp_t3_hdr_t *) (packet + sizeof(sr_ethernet_hdr_t)
						+ sizeof(sr_ip_hdr_t)),
				outgoing_len - sizeof(sr_ethernet_hdr_t) - sizeof(sr_ip_hdr_t));
	} else {
		memcpy(icmp_send_hdr->data, org_ip_hdr, ICMP_DATA_SIZE);
	}

	icmp_send_hdr->icmp_type = icmp_type;
	icmp_send_hdr->icmp_code = icmp_code;
	icmp_send_hdr->icmp_sum = 0;
	if (icmp_type == 0x00) {
		icmp_send_hdr->icmp_sum = cksum(icmp_send_hdr,
				len - sizeof(sr_ethernet_hdr_t) - sizeof(sr_ip_hdr_t));
	} else {
		icmp_send_hdr->icmp_sum = cksum(icmp_send_hdr,
				sizeof(sr_icmp_t3_hdr_t));
	}

	memcpy(ip_send_hdr, org_ip_hdr, sizeof(sr_ip_hdr_t));
	ip_send_hdr->ip_dst = org_ip_hdr->ip_src;
	ip_send_hdr->ip_src = source_ip;
	ip_send_hdr->ip_p = ip_protocol_icmp;
	ip_send_hdr->ip_len = htons(outgoing_len - sizeof(sr_ethernet_hdr_t));
	ip_send_hdr->ip_ttl = INIT_TTL;
	ip_send_hdr->ip_sum = 0;
	ip_send_hdr->ip_sum = cksum(ip_send_hdr, sizeof(sr_ip_hdr_t));

	memcpy(eth_send_hdr->ether_shost, outgoing_interface->addr,
			sizeof(uint8_t) * ETHER_ADDR_LEN);
	memcpy(eth_send_hdr->ether_dhost, org_eth_hdr->ether_shost,
			sizeof(uint8_t) * ETHER_ADDR_LEN);
	eth_send_hdr->ether_type = htons(ethertype_ip);
	sr_send_packet(sr, icmp_send_packet, outgoing_len, rec_interface);
	printf("Sent ICMP Packet\n");
	free(icmp_send_packet);
	return;
}

void send_icmp11_packet(struct sr_instance* sr, uint8_t * packet/* lent */,
		unsigned int len, char* rec_interface/* lent */, uint8_t icmp_type,
		uint8_t icmp_code, struct sr_if *dest_interface) {

	int header_len = sizeof(sr_ip_hdr_t) + sizeof(sr_ethernet_hdr_t)
			+ sizeof(sr_icmp_t11_hdr_t);
	int outgoing_len;
	if (icmp_type == 0x00) {
		outgoing_len = len;
	} else {
		outgoing_len = header_len;
	}

	uint8_t *icmp_send_packet = (uint8_t *) malloc(outgoing_len);
	memset(icmp_send_packet, 0, sizeof(uint8_t) * outgoing_len);

	sr_ethernet_hdr_t *org_eth_hdr = (sr_ethernet_hdr_t *) packet;
	sr_ip_hdr_t *org_ip_hdr = (sr_ip_hdr_t *) (packet
			+ sizeof(sr_ethernet_hdr_t));

	sr_icmp_t11_hdr_t *icmp_send_hdr = (sr_icmp_t11_hdr_t *) (icmp_send_packet
			+ sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t));
	sr_ip_hdr_t *ip_send_hdr = (sr_ip_hdr_t *) (icmp_send_packet
			+ sizeof(sr_ethernet_hdr_t));
	sr_ethernet_hdr_t *eth_send_hdr = (sr_ethernet_hdr_t *) icmp_send_packet;

	struct sr_if *outgoing_interface = sr_get_interface(sr, rec_interface);
	uint32_t source_ip = outgoing_interface->ip;
	if (dest_interface) {
		source_ip = dest_interface->ip;
	}

	if (icmp_type == 0x00) {
		memcpy(icmp_send_hdr,
				(sr_icmp_t11_hdr_t *) (packet + sizeof(sr_ethernet_hdr_t)
						+ sizeof(sr_ip_hdr_t)),
				outgoing_len - sizeof(sr_ethernet_hdr_t) - sizeof(sr_ip_hdr_t));
	} else {
		memcpy(icmp_send_hdr->data, org_ip_hdr, ICMP_DATA_SIZE);
	}

	icmp_send_hdr->icmp_type = icmp_type;
	icmp_send_hdr->icmp_code = icmp_code;
	icmp_send_hdr->icmp_sum = 0;
	if (icmp_type == 0x00) {
		icmp_send_hdr->icmp_sum = cksum(icmp_send_hdr,
				len - sizeof(sr_ethernet_hdr_t) - sizeof(sr_ip_hdr_t));
	} else {
		icmp_send_hdr->icmp_sum = cksum(icmp_send_hdr,
				sizeof(sr_icmp_t11_hdr_t));
	}

	memcpy(ip_send_hdr, org_ip_hdr, sizeof(sr_ip_hdr_t));
	ip_send_hdr->ip_dst = org_ip_hdr->ip_src;
	ip_send_hdr->ip_src = source_ip;
	ip_send_hdr->ip_p = ip_protocol_icmp;
	ip_send_hdr->ip_len = htons(outgoing_len - sizeof(sr_ethernet_hdr_t));
	ip_send_hdr->ip_ttl = INIT_TTL;
	ip_send_hdr->ip_sum = 0;
	ip_send_hdr->ip_sum = cksum(ip_send_hdr, sizeof(sr_ip_hdr_t));

	memcpy(eth_send_hdr->ether_shost, outgoing_interface->addr,
			sizeof(uint8_t) * ETHER_ADDR_LEN);
	memcpy(eth_send_hdr->ether_dhost, org_eth_hdr->ether_shost,
			sizeof(uint8_t) * ETHER_ADDR_LEN);
	eth_send_hdr->ether_type = htons(ethertype_ip);
	sr_send_packet(sr, icmp_send_packet, outgoing_len, rec_interface);
	printf("Sent ICMP Packet\n");
	free(icmp_send_packet);
	return;
}

void handle_ip_packet(struct sr_instance* sr, uint8_t * packet/* lent */,
		unsigned int len, char* interface/* lent */) {
	/*Sanity Check*/
	if (sizeof(sr_ip_hdr_t) + sizeof(sr_ethernet_hdr_t) > len) {
		printf("IP Packet not long enough.\n");
		return;
	}

	sr_ip_hdr_t *ip_hdr = (sr_ip_hdr_t *) (packet + sizeof(sr_ethernet_hdr_t));
	sr_icmp_t3_hdr_t *icmp_hdr = (sr_icmp_t3_hdr_t *) (packet
			+ sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t));

	uint16_t original_checksum = ip_hdr->ip_sum;
	ip_hdr->ip_sum = 0;
	uint16_t new_checksum = cksum(ip_hdr, sizeof(sr_ip_hdr_t));
	if (original_checksum != new_checksum) {
		ip_hdr->ip_sum = original_checksum;
		printf("IP Header checksum failed.\n");
		return;
	} else {
		ip_hdr->ip_sum = new_checksum;
	}

	struct sr_if *dest_interface = get_interface_from_ip(sr, ip_hdr->ip_dst);
	if (dest_interface) {
		uint32_t curr_status = sr_obtain_interface_status(sr,
				dest_interface->name);
		int network_down = 0;
		struct sr_if *head = sr->if_list;
		while (head) {
			if (((head->ip & head->mask) == (ip_hdr->ip_dst & head->mask))
					&& (head->status == 0)) {
				network_down = 1;
			}
			head = head->next;
		}
		if ((curr_status == 0) || (network_down == 1)) {
			send_icmp_packet(sr, packet, len, interface, 3, 0, dest_interface);
			return;
		}
	}

	if (dest_interface || (inet_addr("255.255.255.255") == ip_hdr->ip_dst)) {
		if (ip_hdr->ip_p == ip_protocol_icmp) {
			/*Sanity Check*/
			if ((sizeof(sr_ip_hdr_t) + sizeof(sr_ethernet_hdr_t)
					+ sizeof(sr_icmp_t3_hdr_t) > len) ||
					(sizeof(sr_ip_hdr_t) + sizeof(sr_ethernet_hdr_t)
										+ sizeof(sr_icmp_t11_hdr_t) > len)) {
				printf("ICMP Packet not long enough.\n");
				return;
			}

			original_checksum = icmp_hdr->icmp_sum;
			icmp_hdr->icmp_sum = 0;
			new_checksum = cksum(icmp_hdr,
					len - sizeof(sr_ethernet_hdr_t) - sizeof(sr_ip_hdr_t));
			if (original_checksum != new_checksum) {
				icmp_hdr->icmp_sum = original_checksum;
				fprintf(stderr, "ICMP Header checksum failed.");
				return;
			} else {
				icmp_hdr->icmp_sum = new_checksum;
			}

			if (icmp_hdr->icmp_type != 8) {
				fprintf(stderr, "Not echo request.");
				return;
			}

			if (ip_hdr->ip_ttl <= 1) {
				printf("TTL Expired\n");
				send_icmp11_packet(sr, packet, len, interface, 11, 0, NULL);
				return;
			}

			/*Send an echo reply back*/
			send_icmp_packet(sr, packet, len, interface, 0x00, 0x00,
					dest_interface);
			return;
		} else if (ip_hdr->ip_p == ip_protocol_udp) {
			printf("Handle UDP Packet\n");
			sr_rip_pkt_t *rip_packet = (sr_rip_pkt_t *) (packet + sizeof(sr_ip_hdr_t)
						+ sizeof(sr_ethernet_hdr_t) + sizeof(sr_udp_hdr_t));
			if (rip_packet->command == 1) { /*request */
				printf("Request RIP Packet Detected\n");
				send_rip_update(sr);
			} else if (rip_packet->command == 2) { /*response*/
				printf("Respose RIP Packet Detected\n");
				/*printf("Calling Update Route Table\n");*/
				update_route_table(sr, ip_hdr, rip_packet, interface);
			} else {
				printf("Not an ICMP Packet\n");
				send_icmp_packet(sr, packet, len, interface, 3, 3,
						dest_interface);
				return;
			}

		} else { /*Not an ICMP Packet*/
			printf("Not an ICMP Packet\n");
			send_icmp_packet(sr, packet, len, interface, 3, 3, dest_interface);
			return;
		}
	} else { /*Packet not for one of my interfaces*/
		if (ip_hdr->ip_ttl <= 1) {
			printf("TTL Expired\n");
			send_icmp_packet(sr, packet, len, interface, 11, 0, NULL);
			return;
		}

		pthread_mutex_lock(&(sr->rt_lock));
		struct sr_rt *next_hop = NULL;
		struct sr_rt *routing_table_node = sr->routing_table;
		while (routing_table_node) {
			if (routing_table_node->metric < INFINITY) {
				if ((routing_table_node->dest.s_addr
						& routing_table_node->mask.s_addr)
						== (ip_hdr->ip_dst & routing_table_node->mask.s_addr)) {
					if (!next_hop
							|| (routing_table_node->mask.s_addr
									> next_hop->mask.s_addr)) {
						next_hop = routing_table_node;
					}
				}
			}
			routing_table_node = routing_table_node->next;
		}
		pthread_mutex_unlock(&(sr->rt_lock));

		if (!next_hop) {
			printf("Couldn't find match in routing table.\n");
			send_icmp_packet(sr, packet, len, interface, 3, 0, NULL);
			return;
		}

		ip_hdr->ip_ttl--;
		ip_hdr->ip_sum = 0;
		ip_hdr->ip_sum = cksum(ip_hdr, sizeof(sr_ip_hdr_t));

		struct sr_arpentry *next_hop_mac;
		if (next_hop->gw.s_addr == 0) {
			next_hop_mac = sr_arpcache_lookup(&(sr->cache),
							ip_hdr->ip_dst);
		} else {
			next_hop_mac = sr_arpcache_lookup(&(sr->cache),
					next_hop->gw.s_addr);
		}

		if (!next_hop_mac) {
			printf("ARP Cache entry not found.");
			struct sr_arpreq *queued_arp_req;
			if (next_hop->gw.s_addr == 0) {
				queued_arp_req = sr_arpcache_queuereq(
									&(sr->cache),ip_hdr->ip_dst, packet, len,
									next_hop->interface);
			} else {
				queued_arp_req = sr_arpcache_queuereq(
						&(sr->cache), next_hop->gw.s_addr, packet, len,
						next_hop->interface);
			}
			handle_arpreq(sr, queued_arp_req);
			return;
		}

		sr_ethernet_hdr_t *ethernet_hdr = (sr_ethernet_hdr_t *) packet;
		memcpy(ethernet_hdr->ether_shost,
				sr_get_interface(sr, next_hop->interface)->addr,
				sizeof(uint8_t) * ETHER_ADDR_LEN);

		memcpy(ethernet_hdr->ether_dhost, next_hop_mac->mac,
				sizeof(uint8_t) * ETHER_ADDR_LEN);

		/*For 0.0.0.0 get mac from dest IP*/

		free(next_hop_mac);

		sr_send_packet(sr, packet, len,
				sr_get_interface(sr, next_hop->interface)->name);
		return;
	}
}

int send_arp(struct sr_instance* sr, char* interface,
		struct sr_if* our_interface, sr_ethernet_hdr_t* org_eth_hdr,
		sr_arp_hdr_t* org_arp_hdr) {

	uint8_t * packet = malloc(sizeof(sr_ethernet_hdr_t) + sizeof(sr_arp_hdr_t));
	uint32_t len = (uint32_t) (sizeof(sr_ethernet_hdr_t) + sizeof(sr_arp_hdr_t));

	memset(packet, 0, len * sizeof(uint8_t));

	sr_ethernet_hdr_t* eth_hdr = (sr_ethernet_hdr_t*) packet;
	sr_arp_hdr_t* arp_hdr = (sr_arp_hdr_t*) (packet + sizeof(sr_ethernet_hdr_t));

	/* put the mac addresses VALUES in the ethernet header */
	memcpy(eth_hdr->ether_dhost, org_eth_hdr->ether_shost,
			ETHER_ADDR_LEN * sizeof(uint8_t));
	memcpy(eth_hdr->ether_shost, our_interface->addr,
			ETHER_ADDR_LEN * sizeof(uint8_t));

	/* convert for ntwk order */
	eth_hdr->ether_type = htons(ethertype_arp);

	memcpy(arp_hdr, org_arp_hdr, sizeof(sr_arp_hdr_t));
	arp_hdr->ar_op = htons(arp_op_reply);
	memcpy(arp_hdr->ar_tha, org_eth_hdr->ether_shost, ETHER_ADDR_LEN);
	memcpy(arp_hdr->ar_sha, our_interface->addr, ETHER_ADDR_LEN);
	arp_hdr->ar_tip = org_arp_hdr->ar_sip;
	arp_hdr->ar_sip = our_interface->ip;
	sr_send_packet(sr, packet, len, interface);
	free(packet);
	return 0;
}

/*
 *Handles ARP packets from sr_handle packet. Assumes: Sanity check.
 * ARP Requests: If MAC matches us, call send_arp_reply
 * ARP Reply: Add MAC to arp cache, send all packets in queue waiting on this request.
 * Return 0 on success, 1 on failure
 * referred to: https://tools.ietf.org/html/rfc826
 */
int sr_handle_arp(struct sr_instance* sr, uint8_t * packet, unsigned int len,
		char* interface) {
	if (sizeof(sr_ethernet_hdr_t) + sizeof(sr_arp_hdr_t) > len) {
		printf("ARP Packet not long enough\n");
		return 1;
	}

	sr_arp_hdr_t *arp_hdr =
			(sr_arp_hdr_t *) (packet + sizeof(sr_ethernet_hdr_t));
	sr_ethernet_hdr_t *eth_hdr = (sr_ethernet_hdr_t *) packet;
	struct sr_if *our_interface = sr_get_interface(sr, interface);

	if (ntohs(arp_hdr->ar_op) == arp_op_request) {
		if (arp_hdr->ar_tip != our_interface->ip) {
			return 0;
		}

		send_arp(sr, interface, our_interface, eth_hdr, arp_hdr);
	} else if (ntohs(arp_hdr->ar_op) == arp_op_reply) {
		struct sr_arpreq *arp_req = sr_arpcache_insert(&(sr->cache),
				arp_hdr->ar_sha, arp_hdr->ar_sip);
		if (arp_req) {
			struct sr_packet* packet = arp_req->packets;
			while (packet) {
				uint8_t *send_packet = packet->buf;
				sr_ethernet_hdr_t* ethernet_hdr =
						(sr_ethernet_hdr_t*) send_packet;
				memcpy(ethernet_hdr->ether_dhost, arp_hdr->ar_sha,
						ETHER_ADDR_LEN * sizeof(uint8_t));
				memcpy(ethernet_hdr->ether_shost, our_interface->addr,
						ETHER_ADDR_LEN * sizeof(uint8_t));
				sr_send_packet(sr, send_packet, packet->len, interface);
				packet = packet->next;
			}
			sr_arpreq_destroy(&(sr->cache), arp_req);
		}
	}
	return 0;
}

/*---------------------------------------------------------------------
 * Method: sr_handlepacket(uint8_t* p,char* interface)
 * Scope:  Global
 *
 * This method is called each time the router receives a packet on the
 * interface.  The packet buffer, the packet length and the receiving
 * interface are passed in as parameters. The packet is complete with
 * ethernet headers.
 *
 * Note: Both the packet buffer and the character's memory are handled
 * by sr_vns_comm.c that means do NOT delete either.  Make a copy of the
 * packet instead if you intend to keep it around beyond the scope of
 * the method call.
 *
 *---------------------------------------------------------------------*/

void sr_handlepacket(struct sr_instance* sr,
        uint8_t * packet/* lent */,
        unsigned int len,
        char* interface/* lent */)
{
  /* REQUIRES */
  assert(sr);
  assert(packet);
  assert(interface);

  printf("*** -> Received packet of length %d \n",len);

  /* fill in code here */
  if (ethertype(packet) == ethertype_arp) {
	  printf("PACKET TYPE ARP\n");
	  sr_handle_arp(sr, packet, len, interface);
  }
  if (ethertype(packet) == ethertype_ip) {
	printf("PACKET TYPE IP\n");
	handle_ip_packet (sr, packet, len, interface);
  }

}/* end sr_ForwardPacket */
