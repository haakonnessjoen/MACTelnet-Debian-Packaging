/*
	Mac-Telnet - Connect to RouterOS or mactelnetd devices via MAC address
	Copyright (C) 2010, Håkon Nessjøen <haakon.nessjoen@gmail.com>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with this program; if not, write to the Free Software Foundation, Inc.,
	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
#include <config.h>
#if defined(__FreeBSD__)
#define __USE_BSD
#define __FAVOR_BSD
#endif
#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <ifaddrs.h>
#if defined(__FreeBSD__) || defined(__APPLE__)
#include <netinet/in.h>
#endif
#include <netinet/ip.h>
#include <netinet/udp.h>
#if defined(__FreeBSD__) || defined(__APPLE__)
#include <net/ethernet.h>
#define ETH_FRAME_LEN (ETHER_MAX_LEN - ETHER_CRC_LEN)
#define ETH_ALEN ETHER_ADDR_LEN
#else
#include <netinet/ether.h>
#endif
#ifdef HAVE_LINUX_NETLINK_H
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#endif
#ifdef __APPLE__
#include <pthread.h>
#include <SystemConfiguration/SystemConfiguration.h>
#endif
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <syslog.h>
#ifndef __linux__
#include <net/if_dl.h>
#include <net/bpf.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#else
#include <linux/if_packet.h>
#endif

#include "protocol.h"
#include "interfaces.h"
#include "utlist.h"

#define _(STRING) gettext(STRING)

struct net_interface *net_get_interface_ptr(struct net_interface **interfaces, char *name, int create) {
	struct net_interface *interface;

	DL_FOREACH(*interfaces, interface) {
		if (strncmp(interface->name, name, 254) == 0) {
			return interface;
		}
	}

	if (create) {
		interface = (struct net_interface *)calloc(1, sizeof(struct net_interface));
		if (interface == NULL) {
			fprintf(stderr, _("Unable to allocate memory for interface\n"));
			exit(1);
		}
		strncpy(interface->name, name, 254);
		interface->name[254] = '\0';
		DL_APPEND(*interfaces, interface);
		return interface;
	}

	return NULL;
}

#ifdef __linux__
static void net_update_mac(struct net_interface *interfaces) {
	unsigned char emptymac[] = {0, 0, 0, 0, 0, 0};
	struct ifreq ifr;
	int tmpsock;
	struct net_interface *interface;

	tmpsock = socket(PF_INET, SOCK_DGRAM, 0);
	if (tmpsock < 0) {
		perror("net_update_mac");
		exit(1);
	}
	DL_FOREACH(interfaces, interface) {
		/* Find interface hardware address from device_name */
		strncpy(ifr.ifr_name, interface->name, 16);

		if (ioctl(tmpsock, SIOCGIFFLAGS, &ifr))
			continue;

		if (!(ifr.ifr_flags & IFF_UP))
			continue;

		if (ioctl(tmpsock, SIOCGIFHWADDR, &ifr) == 0) {
			/* Fetch mac address */
			memcpy(interface->mac_addr, ifr.ifr_hwaddr.sa_data, ETH_ALEN);
			if (memcmp(interface->mac_addr, &emptymac, ETH_ALEN) != 0) {
				interface->has_mac = 1;
			}
		}
	}
	close(tmpsock);
}

static int get_device_index(char *device_name) {
	struct ifreq ifr;
	int tmpsock;

	tmpsock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	/* Find interface index from device_name */
	strncpy(ifr.ifr_name, device_name, 16);
	if (ioctl(tmpsock, SIOCGIFINDEX, &ifr) != 0) {
		return -1;
	}

	/* Return interface index */
	return ifr.ifr_ifindex;
}
#endif

int net_get_interfaces(struct net_interface **interfaces) {
	static struct ifaddrs *int_addrs;
	static const struct ifaddrs *ifaddrsp;
	const struct sockaddr_in *dl_addr;
	int found = 0;

	if (getifaddrs(&int_addrs) < 0) {
		perror("getifaddrs");
		exit(1);
	}

	for (ifaddrsp = int_addrs; ifaddrsp; ifaddrsp = ifaddrsp->ifa_next) {
		dl_addr = (const struct sockaddr_in *)ifaddrsp->ifa_addr;

		if (ifaddrsp->ifa_addr == NULL)
			continue;

#ifdef __linux__
		if (ifaddrsp->ifa_addr->sa_family == AF_INET || ifaddrsp->ifa_addr->sa_family == AF_PACKET) {
#else
		if (ifaddrsp->ifa_addr->sa_family == AF_INET) {
#endif
			struct net_interface *interface = net_get_interface_ptr(interfaces, ifaddrsp->ifa_name, 1);
			if (interface != NULL) {
				found++;

				if (ifaddrsp->ifa_addr->sa_family == AF_INET) {
					memcpy(interface->ipv4_addr, &dl_addr->sin_addr, IPV4_ALEN);
				} else {
					memset(interface->ipv4_addr, 0, IPV4_ALEN);
				}
			}
#ifdef __linux__
			interface->ifindex = get_device_index(interface->name);
#endif
		}
#ifndef __linux__
		{
			unsigned char emptymac[] = {0, 0, 0, 0, 0, 0};
			struct sockaddr_dl *sdl = (struct sockaddr_dl *)ifaddrsp->ifa_addr;

			if (sdl->sdl_alen == ETH_ALEN) {
				struct net_interface *interface = net_get_interface_ptr(interfaces, ifaddrsp->ifa_name, 1);
				memcpy(interface->mac_addr, LLADDR(sdl), ETH_ALEN);
				if (interface != NULL && memcmp(interface->mac_addr, &emptymac, ETH_ALEN) != 0) {
					interface->has_mac = 1;
				}
			}
		}
#endif
	}
	freeifaddrs(int_addrs);

#ifdef __linux__
	net_update_mac(*interfaces);
#endif

#if 0
	{
		struct net_interface *interface;
		DL_FOREACH(*interfaces, interface) {
			struct in_addr *addr =
			  (struct in_addr *)interface->ipv4_addr;

			printf("Interface %s:\n", interface->name);
			printf("\tIP: %s\n", inet_ntoa(*addr));
			printf("\tMAC: %s\n",
			  ether_ntoa((struct ether_addr *)interface->mac_addr));
#ifdef __linux__
			printf("\tIfIndex: %d\n", interface->ifindex);
#endif
			printf("\n");
		}
	}
#endif
	return found;
}

int should_refresh_interfaces() {
	static time_t last_refresh = 0;
	time_t now = time(NULL);

	if (now - last_refresh > 1) {
		last_refresh = now;
		return 1;
	}

	return 0;
}

#if defined(__linux__) && defined(FROM_MACTELNETD) && defined(HAVE_LINUX_NETLINK_H)

int get_netlink_fd() {
	int fd;
	struct sockaddr_nl addr;
	struct nlmsghdr *nh;
	char buf[BUFSIZ];

	fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (fd < 0) {
		perror("netlink:socket");
		exit(1);
	}
	memset(&addr, 0, sizeof(addr));
	addr.nl_family = AF_NETLINK;
	addr.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR;

	int err = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
	if (err < 0) {
		perror("netlink:bind");
		exit(1);
	}

	return fd;
}

void read_netlink(int fd) {
	static char buf[BUFSIZ];

	memset(buf, 0, sizeof(buf));
	recv(fd, buf, sizeof(buf), 0);
	/*
	  We ignore the result for now, we will just do a full refresh of the
	  network interfaces
	*/
}

#endif

#if defined(__APPLE__) && defined(FROM_MACTELNETD)

void callback(SCDynamicStoreRef store, CFArrayRef changedKeys, void *info) {
	if (!should_refresh_interfaces()) {
		return;
	}

	syslog(LOG_NOTICE, _("Network change detected"));

	// Handle the network change
	pid_t pid = getpid();
	kill(pid, SIGHUP);
}

void *init_network_watcher_thread(void *arg) {
	SCDynamicStoreRef store;
	CFRunLoopSourceRef source;

	store = SCDynamicStoreCreate(NULL, CFSTR("no.lunatic.mactelnet.NetworkChangeDetector"), callback, NULL);
	source = SCDynamicStoreCreateRunLoopSource(NULL, store, 0);

	CFRunLoopAddSource(CFRunLoopGetCurrent(), source, kCFRunLoopDefaultMode);

	CFStringRef keysToMonitor[] = {CFSTR("State:/Network/Interface"), CFSTR("State:/Network/Global/IPv4")};

	CFArrayRef keysArray = CFArrayCreate(NULL, (const void **)keysToMonitor,
										 sizeof(keysToMonitor) / sizeof(keysToMonitor[0]), &kCFTypeArrayCallBacks);
	SCDynamicStoreSetNotificationKeys(store, keysArray, NULL);

	CFRunLoopRun();

	CFRelease(store);
	CFRelease(source);

	return NULL;
}

void init_network_watcher() {
	pthread_t thread;
	int result = pthread_create(&thread, NULL, init_network_watcher_thread, NULL);
	if (result != 0) {
		fprintf(stderr, _("Error creating network watcher thread: %s\n"), strerror(result));
		exit(EXIT_FAILURE);
	}
}
#endif

unsigned short in_cksum(unsigned short *addr, int len) {
	int nleft = len;
	int sum = 0;
	unsigned short *w = addr;
	unsigned short answer = 0;

	while (nleft > 1) {
		sum += *w++;
		nleft -= 2;
	}

	if (nleft == 1) {
		*(unsigned char *)(&answer) = *(unsigned char *)w;
		sum += answer;
	}

	sum = (sum >> 16) + (sum & 0xFFFF);
	sum += (sum >> 16);
	answer = ~sum;
	return (answer);
}

unsigned short udp_sum_calc(unsigned char *src_addr, unsigned char *dst_addr, unsigned char *data, unsigned short len) {
	unsigned short prot_udp = 17;
	unsigned short padd = 0;
	unsigned short word16;
	unsigned int sum = 0;
	int i;

	/* Padding ? */
	padd = (len % 2);
	if (padd) {
		data[len] = 0;
	}

	/* header+data */
	for (i = 0; i < len + padd; i += 2) {
		word16 = ((data[i] << 8) & 0xFF00) + (data[i + 1] & 0xFF);
		sum += word16;
	}

	/* source ip */
	for (i = 0; i < IPV4_ALEN; i += 2) {
		word16 = ((src_addr[i] << 8) & 0xFF00) + (src_addr[i + 1] & 0xFF);
		sum += word16;
	}

	/* dest ip */
	for (i = 0; i < IPV4_ALEN; i += 2) {
		word16 = ((dst_addr[i] << 8) & 0xFF00) + (dst_addr[i + 1] & 0xFF);
		sum += word16;
	}

	sum += prot_udp + len;

	while (sum >> 16)
		sum = (sum & 0xFFFF) + (sum >> 16);

	sum = ~sum;

	if (sum == 0)
		sum = 0xFFFF;

	return (unsigned short)sum;
}

#if !defined(__linux__)
int net_find_bpf_device() {
	char dev[11];
	for (int i = 0; i < 99; ++i) {
		sprintf(dev, "/dev/bpf%i", i);
		int fd = open(dev, O_RDWR);
		if (fd != -1) {
			return fd;
		}
	}
	return -1;
}
#endif

int net_init_raw_socket() {
	int fd;

#ifdef __linux__
	/* Transmit raw packets with this socket */
	fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	if (fd < 0) {
		perror("raw_socket");
		exit(1);
	}
#else
	/* Transmit raw packets with bpf */
	fd = net_find_bpf_device();
	if (fd <= 0) {
		perror("open_bpf");
		exit(1);
	}
#endif

	return fd;
}

int net_send_udp(const int fd, struct net_interface *interface, const unsigned char *sourcemac,
				 const unsigned char *destmac, const struct in_addr *sourceip, const int sourceport,
				 const struct in_addr *destip, const int destport, const unsigned char *data, const int datalen) {
#ifdef __linux__
	struct sockaddr_ll socket_address;
#endif
	/*
	 * Create a buffer for the full ethernet frame
	 * and align header pointers to the correct positions.
	 */
	static unsigned char stackbuf[ETH_FRAME_LEN];
	void *buffer = (void *)&stackbuf;
#if defined(__FreeBSD__) || defined(__APPLE__)
	struct ether_header *eh = (struct ether_header *)buffer;
	struct ip *ip = (struct ip *)(buffer + 14);
#else
	struct ethhdr *eh = (struct ethhdr *)buffer;
	struct iphdr *ip = (struct iphdr *)(buffer + 14);
#endif
	struct udphdr *udp = (struct udphdr *)(buffer + 14 + 20);
	unsigned char *rest = (unsigned char *)(buffer + 20 + 14 + sizeof(struct udphdr));

	/* Avoid integer overflow in check */
	if (datalen > ETH_FRAME_LEN - ((void *)rest - (void *)buffer)) {
		fprintf(stderr, _("packet size too large\n"));
		return 0;
	}

	static unsigned int id = 1;
	int send_result = 0;

	/* Abort if we couldn't allocate enough memory */
	if (buffer == NULL) {
		perror("malloc");
		exit(1);
	}

	/* Init ethernet header */
#if defined(__FreeBSD__) || defined(__APPLE__)
	memcpy(eh->ether_shost, sourcemac, ETH_ALEN);
	memcpy(eh->ether_dhost, destmac, ETH_ALEN);
	eh->ether_type = htons(ETHERTYPE_IP);
#else
	memcpy(eh->h_source, sourcemac, ETH_ALEN);
	memcpy(eh->h_dest, destmac, ETH_ALEN);
	eh->h_proto = htons(ETH_P_IP);
#endif

#ifdef __linux__
	/* Init SendTo struct */
	socket_address.sll_family = AF_PACKET;
	socket_address.sll_protocol = htons(ETH_P_IP);
	socket_address.sll_ifindex = interface->ifindex;
	socket_address.sll_hatype = ARPHRD_ETHER;
	socket_address.sll_pkttype = PACKET_OTHERHOST;
	socket_address.sll_halen = ETH_ALEN;

	memcpy(socket_address.sll_addr, eh->h_source, ETH_ALEN);
	socket_address.sll_addr[6] = 0x00; /*not used*/
	socket_address.sll_addr[7] = 0x00; /*not used*/
#endif

	/* Init IP Header */
#if defined(__FreeBSD__) || defined(__APPLE__)
	ip->ip_v = 4;
	ip->ip_hl = 5;
	ip->ip_tos = 0x10;
	ip->ip_len = htons(datalen + 8 + 20);
	ip->ip_id = htons(id++);
	ip->ip_off = htons(0x4000);
	ip->ip_ttl = 64;
	ip->ip_p = 17; /* UDP */
	ip->ip_sum = 0;
	ip->ip_src.s_addr = sourceip->s_addr;
	ip->ip_dst.s_addr = destip->s_addr;
#else
	ip->version = 4;
	ip->ihl = 5;
	ip->tos = 0x10;
	ip->tot_len = htons(datalen + 8 + 20);
	ip->id = htons(id++);
	ip->frag_off = htons(0x4000);
	ip->ttl = 64;
	ip->protocol = 17; /* UDP */
	ip->check = 0x0000;
	ip->saddr = sourceip->s_addr;
	ip->daddr = destip->s_addr;
#endif

	/* Calculate checksum for IP header */
#if defined(__FreeBSD__) || defined(__APPLE__)
	ip->ip_sum = in_cksum((unsigned short *)ip, sizeof(struct ip));
#else
	ip->check = in_cksum((unsigned short *)ip, sizeof(struct iphdr));
#endif

	/* Init UDP Header */
#if defined(__FreeBSD__) || defined(__APPLE__)
	udp->uh_sport = htons(sourceport);
	udp->uh_dport = htons(destport);
	udp->uh_ulen = htons(sizeof(struct udphdr) + datalen);
	udp->uh_sum = 0;
#else
	udp->source = htons(sourceport);
	udp->dest = htons(destport);
	udp->len = htons(sizeof(struct udphdr) + datalen);
	udp->check = 0;
#endif

	/* Insert actual data */
	memcpy(rest, data, datalen);

	/* Add UDP checksum */
#if defined(__FreeBSD__) || defined(__APPLE__)
	udp->uh_sum = udp_sum_calc((unsigned char *)&(ip->ip_src.s_addr), (unsigned char *)&(ip->ip_dst.s_addr),
							   (unsigned char *)udp, sizeof(struct udphdr) + datalen);
	udp->uh_sum = htons(udp->uh_sum);
#else
	udp->check = udp_sum_calc((unsigned char *)&(ip->saddr), (unsigned char *)&(ip->daddr), (unsigned char *)udp,
							  sizeof(struct udphdr) + datalen);
	udp->check = htons(udp->check);
#endif

#ifdef __linux__
	/* Send the packet */
	send_result =
		sendto(fd, buffer, datalen + 8 + 14 + 20, 0, (struct sockaddr *)&socket_address, sizeof(socket_address));
	if (send_result == -1)
		perror("sendto");
#else
	{
		struct ifreq req_if;

		/* Pick device to send through */
		strcpy(req_if.ifr_name, interface->name);
		if (ioctl(fd, BIOCSETIF, &req_if) > 0) {
			perror("ioctl_BIOCSETIF");
			exit(1);
		}
	}

	send_result = write(fd, buffer, datalen + 8 + 14 + 20);
	if (send_result == -1) {
		// perror("bpf_write");
		return 0;
	}
#endif

	/* Return amount of _data_ bytes sent */
	if (send_result - 8 - 14 - 20 < 0) {
		return 0;
	}

	return send_result - 8 - 14 - 20;
}
