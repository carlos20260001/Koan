/*
 * Copyright 2006-2019, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *		James Woodcock
 */


#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <SupportDefs.h>

#include <net_stat.h>
#include <syscalls.h>


extern const char* __progname;
const char* kProgramName = __progname;

static int sResolveNames = 1;

enum filter_flags {
	FILTER_FAMILY_MASK		= 0x0000ff,
	FILTER_PROTOCOL_MASK	= 0x00ff00,
	FILTER_STATE_MASK		= 0xff0000,

	// Families
	FILTER_AF_INET			= 0x000001,
	FILTER_AF_INET6			= 0x000002,
	FILTER_AF_UNIX			= 0x000004,

	// Protocols
	FILTER_IPPROTO_TCP		= 0x000100,
	FILTER_IPPROTO_UDP		= 0x000200,

	// States
	FILTER_STATE_LISTEN		= 0x010000,
};

// AF_INET/AF_INET6 families
static void inet_print_address(sockaddr* address);


static void
inet_print_address(sockaddr* address)
{
	if ((address->sa_family != AF_INET && address->sa_family != AF_INET6) ||
		address->sa_len == 0) {
		printf("%-21s", "-");
		return;
	}

	bool resolvedHostName = false;
	char hostName[128];
	if (sResolveNames) {
		// First try to resolve the host name
		if (getnameinfo(address, address->sa_len, hostName, sizeof(hostName),
			NULL, 0, NI_NAMEREQD) == 0) {
			resolvedHostName = true;
		}
	}
	// Now get the port and (optionally) the numeric host address
	char port[32];
	if (getnameinfo(address, address->sa_len,
		resolvedHostName ? NULL : hostName,
		resolvedHostName ? 0 : sizeof(hostName),
		port, sizeof(port),
		NI_NUMERICHOST | (sResolveNames ? 0 : NI_NUMERICSERV)) != 0) {
		printf("%-21s", "-");
		return;
	}

	char buffer[128];
	int length;
	if (address->sa_family != AF_INET
		|| ((sockaddr_in*)address)->sin_addr.s_addr != INADDR_ANY) {
		length = snprintf(buffer, sizeof(buffer),
			resolvedHostName ? "%s" : "[%s]", hostName);
	} else {
		// Special handling for IPv4 any address
		strcpy(buffer, "*");
		length = 1;
	}
	if (length < sizeof(buffer)) {
		length += snprintf(buffer + length, sizeof(buffer) - length,
			":%s", port);
	}

	printf("%-21s", buffer);
}


//	#pragma mark -


void
usage(int status)
{
	printf("Usage: %s [-nhs]\n", kProgramName);
	printf("Options:\n");
	printf("	-n	don't resolve names\n");
	printf("	-s	show the statistics of the protocols\n");
	printf("	-h	this help\n");
	printf("Filter options:\n");
	printf("	-4	IPv4\n");
	printf("	-6	IPv6\n");
	printf("	-x	Unix\n");
	printf("	-t	TCP\n");
	printf("	-u	UDP\n");
	printf("	-l	listen state\n");

	exit(status);
}


static int
print_statistics()
{
	net_protocol_stats stats;
	memset(&stats, 0, sizeof(stats));
	status_t status = _kern_generic_syscall(NET_STAT_SYSCALLS,
		NET_STAT_GET_PROTOCOL_STATS, &stats, sizeof(stats));
	if (status != B_OK) {
		fprintf(stderr, "%s: could not get the statistics: %s\n",
			kProgramName, strerror(status));
		return 1;
	}

#define LINE(value, text) \
	printf("	%" B_PRIu64 " " text "\n", (uint64)(value))

	printf("ip:\n");
	LINE(stats.ip_received, "packets received");
	LINE(stats.ip_header_errors, "with header errors");
	LINE(stats.ip_no_protocol, "for unknown protocols");
	LINE(stats.ip_fragments_received, "fragments received");
	LINE(stats.ip_reassembled, "packets reassembled");
	LINE(stats.ip_reassembly_dropped, "fragments dropped (timeout or limit)");
	LINE(stats.ip_sent, "packets sent");

	printf("udp:\n");
	LINE(stats.udp_received, "datagrams received");
	LINE(stats.udp_checksum_errors, "with bad checksums");
	LINE(stats.udp_no_port, "for ports nobody listens on");
	LINE(stats.udp_receive_buffer_errors,
		"dropped because the socket's queue was full");
	LINE(stats.udp_sent, "datagrams sent");

	printf("tcp:\n");
	LINE(stats.tcp_received, "segments received");
	LINE(stats.tcp_checksum_errors, "with bad checksums");
	LINE(stats.tcp_sent, "segments sent");
	LINE(stats.tcp_retransmitted, "segments retransmitted");
	LINE(stats.tcp_resets_sent, "resets sent");
	LINE(stats.tcp_challenge_acks, "challenge ACKs sent (RFC 5961)");
	LINE(stats.tcp_connections_refused,
		"connections refused (listen queue full)");
	LINE(stats.tcp_syn_cookies_sent, "SYN cookies sent");
	LINE(stats.tcp_syn_cookies_accepted, "connections made from SYN cookies");
	LINE(stats.tcp_segments_coalesced,
		"segments coalesced into the one before them");
	LINE(stats.tcp_retransmit_timeouts, "retransmit timeouts");
	LINE(stats.tcp_fast_recoveries, "fast recoveries");
	LINE(stats.tcp_loss_probes, "tail loss probes");
	LINE(stats.tcp_out_of_window_drops,
		"segments dropped outside the receive window");
	LINE(stats.tcp_out_of_order_drops,
		"out-of-order segments dropped (queue buffer limit)");

	printf("raw:\n");
	LINE(stats.raw_receive_buffer_errors,
		"packets dropped because the socket's queue was full");
#undef LINE

	return 0;
}


//	#pragma mark -


int
main(int argc, char** argv)
{
	int optionIndex = 0;
	int opt;
	int filter = 0;

	const static struct option kLongOptions[] = {
		{"help", no_argument, 0, 'h'},
		{"numeric", no_argument, 0, 'n'},

		{"inet", no_argument, 0, '4'},
		{"inet6", no_argument, 0, '6'},
		{"unix", no_argument, 0, 'x'},

		{"tcp", no_argument, 0, 't'},
		{"udp", no_argument, 0, 'u'},

		{"listen", no_argument, 0, 'l'},
		{"statistics", no_argument, 0, 's'},

		{0, 0, 0, 0}
	};

	do {
		opt = getopt_long(argc, argv, "hn46xtuls", kLongOptions,
			&optionIndex);
		switch (opt) {
			case -1:
				// end of arguments, do nothing
				break;

			case 'n':
				sResolveNames = 0;
				break;

			// Family filter
			case '4':
				filter |= FILTER_AF_INET;
				break;
			case '6':
				filter |= FILTER_AF_INET6;
				break;
			case 'x':
				filter |= FILTER_AF_UNIX;
				break;
			// Protocol filter
			case 't':
				filter |= FILTER_IPPROTO_TCP;
				break;
			case 'u':
				filter |= FILTER_IPPROTO_UDP;
				break;
			// State filter
			case 'l':
				filter |= FILTER_STATE_LISTEN;
				break;

			case 's':
				return print_statistics();

			case 'h':
			default:
				usage(0);
				break;
		}
	} while (opt != -1);

	bool printProgram = true;
		// TODO: add some more program options... :-)

	printf("Proto  Recv-Q Send-Q Local Address         Foreign Address       "
		"State        Program\n");

	uint32 cookie = 0;
	int family = -1;
	net_stat stat;
	while (_kern_get_next_socket_stat(family, &cookie, &stat) == B_OK) {
		// Filter families
		if ((filter & FILTER_FAMILY_MASK) != 0) {
			if ((stat.family != AF_INET && stat.family != AF_INET6
					&& stat.family != AF_UNIX)
				|| ((filter & FILTER_AF_INET) == 0 && stat.family == AF_INET)
				|| ((filter & FILTER_AF_INET6) == 0 && stat.family == AF_INET6)
				|| ((filter & FILTER_AF_UNIX) == 0 && stat.family == AF_UNIX))
				continue;
		}
		// Filter protocols
		if ((filter & FILTER_PROTOCOL_MASK) != 0) {
			if ((stat.protocol != IPPROTO_TCP && stat.protocol != IPPROTO_UDP)
				|| ((filter & FILTER_IPPROTO_TCP) == 0
					&& stat.protocol == IPPROTO_TCP)
				|| ((filter & FILTER_IPPROTO_UDP) == 0
					&& stat.protocol == IPPROTO_UDP))
				continue;
		}
		if ((filter & FILTER_STATE_MASK) != 0) {
			if ((filter & FILTER_STATE_LISTEN) == 0
				|| strcmp(stat.state, "listen") != 0)
				continue;
		}

		protoent* proto = getprotobynumber(stat.protocol);
		if (proto != NULL)
			printf("%-6s ", proto->p_name);
		else
			printf("%-6d ", stat.protocol);

		printf("%6lu ", stat.receive_queue_size);
		printf("%6lu ", stat.send_queue_size);

		inet_print_address((sockaddr*)&stat.address);
		printf(" ");
		inet_print_address((sockaddr*)&stat.peer);
		printf(" %-12s ", stat.state);

		team_info info;
		if (printProgram && get_team_info(stat.owner, &info) == B_OK) {
			// remove arguments
			char* name = strchr(info.args, ' ');
			if (name != NULL)
				name[0] = '\0';

			// remove path name
			name = strrchr(info.args, '/');
			if (name != NULL)
				name++;
			else
				name = info.args;

			printf("%" B_PRId32 "/%s\n", stat.owner, name);
		} else
			printf("%" B_PRId32 "\n", stat.owner);
	}

	return 0;
}

