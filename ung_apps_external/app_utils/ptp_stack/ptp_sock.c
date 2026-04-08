#include "wnp.h"
#include "unpifi.h"
#include "datatype.h"

#ifndef _SYS_SOCKET_H
#include <sys\timeb.h>
#include <process.h>
#include "ip_icmp.h"

#else
#include <net/if.h>
#include <linux/sockios.h>
#include <linux/if_packet.h>
#define ARPHRD_ETHER 	1		/* Ethernet 10Mbps		*/
#include <linux/if_ether.h>
#include <sys/timeb.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include "wrapthread.h"

#ifndef IPV6_TCLASS
#define IPV6_TCLASS		67
#endif

#define _PATH_PROCNET_IFINET6           "/proc/net/if_inet6"

struct ipv6_info {
	char devname[20];
	struct sockaddr_in6 addr;
	int plen;
	int scope;
	int if_idx;
};

#define _PATH_SYSNET_DEV		"/sys/class/net/"

#define NETDEV_ADDRESS			"address"
#define NETDEV_FLAGS			"flags"
#define NETDEV_IFINDEX			"ifindex"
#define NETDEV_OPERSTATE		"operstate"

#endif


#if defined(_MSC_VER)
typedef uint8_t u8;
typedef int16_t s16;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int64_t s64;
typedef uint64_t u64;

#include <packon.h>
#define __packed
#endif

#if defined(__GNUC__)
#ifndef __packed
#define __packed __attribute__((packed))
#endif
#endif

#define PTP_PROTO		0x88F7

#if defined(_MSC_VER)
#include <packoff.h>
#endif


int gWaitDelay = 100;

char *PTP_ip_addr = "224.0.1.129";
char *P2P_ip_addr = "224.0.0.107";
char *PTP_ip_addr6_const = "ff02::0181";
char *P2P_ip_addr6_const = "ff02::6b";
char PTP_ip_addr6[20];
char P2P_ip_addr6[20];

#define PTP_EVENT_PORT			319
#define PTP_GENERAL_PORT		320

int ip_family;
int ipv6_interface = 0;
int ptp_proto = PTP_PROTO;

SOCKET event_fd;
SOCKET general_fd;
SOCKET uni_event_fd;
SOCKET uni_general_fd;
SOCKET management4_fd;
SOCKET eth_fd;

SOCKET *sptr;
char devname[40];

int event_msgs[] = {
	SYNC_MSG,
	DELAY_REQ_MSG,
	PDELAY_REQ_MSG,
	PDELAY_RESP_MSG,
	-1
};

int general_msgs[] = {
	FOLLOW_UP_MSG,
	DELAY_RESP_MSG,
	PDELAY_RESP_FOLLOW_UP_MSG,
	-1
};

int ptp_2step = 0;
int ptp_alternate = 0;
int ptp_unicast = 0;

struct sockaddr_in event_addr;
struct sockaddr_in general_addr;
struct sockaddr_in p2p_event_addr;
struct sockaddr_in p2p_general_addr;
struct sockaddr_in management_addr;
struct sockaddr_in6 event_addr6;
struct sockaddr_in6 general_addr6;
struct sockaddr_in6 p2p_event_addr6;
struct sockaddr_in6 p2p_general_addr6;
struct sockaddr_in6 management_addr6;
#ifdef _SYS_SOCKET_H
struct sockaddr_ll eth_pdelay_addr;
struct sockaddr_ll eth_others_addr;
u8 *eth_pdelay_buf;
u8 *eth_others_buf;

u8 eth_pdelay[] = { 0x01, 0x80, 0xC2, 0x00, 0x00, 0x0E };
u8 eth_others[] = { 0x01, 0x1B, 0x19, 0x00, 0x00, 0x00 };

u8 hw_addr[ETH_ALEN];

struct dev_info ptpdev;
struct {
	void *fd;
} dev[2];
int ptp_hw;

#endif

u8 host_addr[16];
u8 host_addr6[16];

typedef struct {
	int fTaskStop;
	int multicast;
	int unicast;
	int management4;
	int *msgs;

#if defined(_WIN32)
	HANDLE hevTaskComplete;
#endif
} TTaskParam, *PTTaskParam;


struct ptp_clock_identity selfClockIdentity;
static int ptp_rx_port;
static u32 ptp_rx_sec;
static u32 ptp_rx_nsec;

void send_msg(struct ptp_msg *msg, int family, u16 vid, int len, u8 index,
	      u8 *dest)
{
	SOCKET sockfd;
	SAI *pservaddr;
	socklen_t servlen;
	char *buf = (char *) msg;
	int ptp_start = 0;

	if (!len)
		len = ntohs(msg->hdr.messageLength);
	if (AF_INET6 == family) {
		buf[len] = 0x15;
		buf[len + 1] = 0x88;
		len += 2;
	}

#ifdef _SYS_SOCKET_H
	if (AF_PACKET == family) {
		sockfd = eth_fd;
		switch (msg->hdr.messageType) {
		case PDELAY_REQ_MSG:
		case PDELAY_RESP_MSG:
		case PDELAY_RESP_FOLLOW_UP_MSG:
			pservaddr = (SAI *) &eth_pdelay_addr;
			servlen = sizeof(eth_pdelay_addr);
			buf = (char *) eth_pdelay_buf;
			break;
		default:
			pservaddr = (SAI *) &eth_others_addr;
			servlen = sizeof(eth_others_addr);
			buf = (char *) eth_others_buf;
			break;
		}
		memcpy(&buf[ETH_ALEN + 3], &selfClockIdentity.addr[5], 3);
#if 1
		inc_mac_addr(&buf[ETH_ALEN + 3], index);
#else
		inc_mac_addr(&buf[ETH_ALEN + 3], 1);
#endif
		ptp_start = 14;
		memcpy(&buf[ptp_start], msg, len);
		len += ptp_start;
	} else
#endif
	switch (msg->hdr.messageType) {
	case SYNC_MSG:
	case DELAY_REQ_MSG:
		sockfd = event_fd;
		if (AF_INET6 == family) {
			pservaddr = (SAI *) &event_addr6;
			servlen = sizeof(event_addr6);
		} else {
			pservaddr = &event_addr;
			servlen = sizeof(event_addr);
		}
		break;
	case PDELAY_REQ_MSG:
	case PDELAY_RESP_MSG:
		sockfd = event_fd;
		if (AF_INET6 == family) {
			pservaddr = (SAI *) &p2p_event_addr6;
			servlen = sizeof(p2p_event_addr6);
		} else {
			pservaddr = &p2p_event_addr;
			servlen = sizeof(p2p_event_addr);
		}
		break;
	case PDELAY_RESP_FOLLOW_UP_MSG:
		sockfd = general_fd;
		if (AF_INET6 == family) {
			pservaddr = (SAI *) &p2p_general_addr6;
			servlen = sizeof(p2p_general_addr6);
		} else {
			pservaddr = &p2p_general_addr;
			servlen = sizeof(p2p_general_addr);
		}
		break;
	case MANAGEMENT_MSG:
		sockfd = general_fd;
		if (AF_INET6 == family) {
			if (ptp_unicast) {
				pservaddr = (SAI *) &general_addr6;
				servlen = sizeof(general_addr6);
			} else {
				pservaddr = (SAI *) &management_addr6;
				servlen = sizeof(management_addr6);
			}
		} else {
			sockfd = management4_fd;
			if (ptp_unicast && AF_INET == ip_family) {
				pservaddr = &general_addr;
				servlen = sizeof(general_addr);
			} else {
				pservaddr = &management_addr;
				servlen = sizeof(management_addr);
			}
		}
		break;
	default:
		sockfd = general_fd;
		if (AF_INET6 == family) {
			pservaddr = (SAI *) &general_addr6;
			servlen = sizeof(general_addr6);
		} else {
			pservaddr = &general_addr;
			servlen = sizeof(general_addr);
		}
		break;
	}
	Sendto(sockfd, buf, len, 0, (SA *) pservaddr, servlen);
}  /* send_msg */

static int get_rx_timestamp(struct ptp_msg_hdr *hdr)
{
#ifdef _SYS_SOCKET_H
	u32 port;
	u32 sec;
	u32 nsec;
	int rc;
	int tx = 0;

	rc = get_msg_info(dev[1].fd, hdr, &tx, &port, &sec, &nsec);
	if (!rc) {
		ptp_rx_port = port;
		ptp_rx_sec = sec;
		ptp_rx_nsec = nsec;
	}
	return rc;
#else
        return 0;
#endif
}

struct ip_info {
	struct sockaddr_in addr;
	struct sockaddr_in6 addr6;
	u8 hwaddr[8];
	int plen;
	int scope;
	int if_idx;
};

#include "avtpdu_1722.c"

static void show_ptp_param_s8(int8_t param, bool newline)
{
	printf("%d ", param);
	if (newline)
		printf(NL);
}

static void show_ptp_param_u8(uint8_t param, bool newline)
{
	printf("%u ", param);
	if (newline)
		printf(NL);
}

static void show_ptp_param_s16(int16_t param, bool newline)
{
	printf("%d ", param);
	if (newline)
		printf(NL);
}

static void show_ptp_param_u16(uint16_t param, bool newline)
{
	printf("%u [%04X] ", param, param);
	if (newline)
		printf(NL);
}

static u8 cfg_index;

void ptp_stack_start_clock(void);
void ptp_stack_stop_clock(void);

static int get_ptp_cmd(FILE *fp)
{
	int count, hcount;
	char p1[40];
	char line[80];
	int len;
	int h1, h2, h3, h4;
	int v1, v2, v3, v4;
	uint8_t val8_1, val8_2, val8_3;
	uint16_t val16_1, val16_2;
	int8_t val_1, val_2;
	int16_t val;
	int cont = 1;

	do {
		printf("ptp> ");
		if (fgets(line, 80, fp) == NULL)
			break;
		p1[0] = '\0';
		len = strlen(line);
		if (!len)
			continue;
                v1 = v2 = v3 = v4 = 0;
		count = sscanf(line, "%s %d %d %d %d",
			p1, &v1, &v2, &v3, &v4);
		hcount = sscanf(line, "%s %d %d %d %d",
			p1, &h1, &h2, &h3, &h4);
		if (!count && !hcount)
			continue;

		if (!strcmp(p1, "c")) {
			if (v1 == 1)
				ptp_stack_start_clock();
			else
				ptp_stack_stop_clock();
		} else if (!strcmp(p1, "autotest")) {
			if (count >= 2)
				automotive_test_mode = !!v1;
			else
				printf("%d"NL, automotive_test_mode);
		} else if (!strcmp(p1, "avb")) {
			test_aed(v1, v2); 
		} else if (!strcmp(p1, "faults")) {
			gptp_get_allowed(&val8_1, &val8_2);
			if (count >= 2)
				;
			else
				show_ptp_param_u8(val8_1, true);
		} else if (!strcmp(p1, "missed")) {
			gptp_get_allowed(&val8_1, &val8_2);
			if (count >= 2)
				;
			else
				show_ptp_param_u8(val8_2, true);
		} else if (!strcmp(p1, "follow_up")) {
			gptp_get_follow_up_timeout(&val8_1);
			if (count >= 2)
				;
			else
				show_ptp_param_u8(val8_1, true);
		} else if (!strcmp(p1, "wait")) {
			gptp_get_initial_wait(&val8_1, &val8_2, &val8_3);
			if (count >= 2) {
#if 0
				if (count < 4)
					val8_3 = NULL;
				if (count < 3)
					val8_2 = NULL;
#endif
			} else {
				show_ptp_param_u8(val8_1, false);
				show_ptp_param_u8(val8_2, false);
				show_ptp_param_u8(val8_3, true);
			}
		}
		if (!strcmp(p1, "index")) {
			if (count >= 2 && v1 >= 1 && v1 <= 3) {
				cfg_index = (uint8_t) v1;
				ptp_stack_stop_clock();
				ptp_stack_start_clock();
			} else {
				printf("%u"NL, cfg_index);
			}
		}
		if (!strcmp(p1, "profile") ||
		    !strcmp(p1, "transport") ||
		    !strcmp(p1, "sdoid") ||
		    !strcmp(p1, "e2e") ||
		    !strcmp(p1, "p2p") ||
		    !strcmp(p1, "gm_capable") ||
		    !strcmp(p1, "two_step") ||
		    !strcmp(p1, "cmlds") ||
		    !strcmp(p1, "clk") ||
		    !strcmp(p1, "prio") ||
		    !strcmp(p1, "domain") ||
		    !strcmp(p1, "ports") ||
		    !strcmp(p1, "pdelay") ||
		    !strcmp(p1, "announce") ||
		    !strcmp(p1, "sync") ||
		    !strcmp(p1, "oper") ||
		    !strcmp(p1, "thresh")) {
			if (count < 2 || v1 < 1 || v1 > 3) {
				printf("need index 1-3"NL);
				continue;
			}
		}
		if (!strcmp(p1, "latency") ||
		    !strcmp(p1, "master") ||
		    !strcmp(p1, "use_delay")) {
			if (count < 2 || v1 < 1 || v1 > 8) {
				printf("need port 1-8"NL);
				continue;
			}
		}
#if 0
		v1--;
#endif
		if (!strcmp(p1, "profile")) {
			gptp_profile_get_profile(v1, &val8_1);
			if (count >= 3)
				;
			else
				show_ptp_param_u8(val8_1, true);
		} else if (!strcmp(p1, "transport")) {
			gptp_profile_get_transport(v1, &val8_1);
			if (count >= 3)
				;
			else
				show_ptp_param_u8(val8_1, true);
		} else if (!strcmp(p1, "sdoid")) {
			gptp_profile_get_sdoid(v1, &val8_1, &val8_2);
			if (count >= 3) {
#if 0
				if (count < 4)
					val8_2 = NULL;
				cfg_ptp_param_u8(val8_1, v2, val8_2, v3, NULL, 0);
#endif
			} else {
				show_ptp_param_u8(val8_1, false);
				show_ptp_param_u8(val8_2, true);
			}
		} else if (!strcmp(p1, "e2e")) {
			gptp_profile_get_e2e_p2p(v1, &val8_1, &val8_2);
			if (count >= 3)
				;
			else
				show_ptp_param_u8(val8_1, true);
		} else if (!strcmp(p1, "p2p")) {
			gptp_profile_get_e2e_p2p(v1, &val8_1, &val8_2);
			if (count >= 3)
				;
			else
				show_ptp_param_u8(val8_2, true);
		} else if (!strcmp(p1, "gm_capable")) {
			gptp_profile_get_gm_capable(v1, &val8_1);
			if (count >= 3)
				;
			else
				show_ptp_param_u8(val8_1, true);
		} else if (!strcmp(p1, "two_step")) {
			gptp_profile_get_two_step(v1, &val8_1);
			if (count >= 3)
				;
			else
				show_ptp_param_u8(val8_1, true);
		} else if (!strcmp(p1, "cmlds")) {
			val8_1 = 0;
			if (count >= 3)
				;
			else
				show_ptp_param_u8(val8_1, true);
		} else if (!strcmp(p1, "clk")) {
			gptp_profile_get_clock_prop(v1, &val8_1, &val8_2,
				&val16_1);
			if (count >= 3) {
#if 0
				if (count < 4)
					val8_2 = NULL;
				cfg_ptp_param_u8(val8_1, v2, val8_2, v3, NULL, 0);
				if (count >= 5)
					cfg_ptp_param_u16(val16_1, v4);
#endif
			} else {
				show_ptp_param_u8(val8_1, false);
				show_ptp_param_u8(val8_2, false);
				show_ptp_param_u16(val16_1, true);
			}
		} else if (!strcmp(p1, "prio")) {
			gptp_profile_get_clock_prio(v1, &val8_1, &val8_2);
			if (count >= 3) {
#if 0
				if (count < 4)
					val8_2 = NULL;
				cfg_ptp_param_u8(val8_1, v2, val8_2, v3, NULL, 0);
#endif
			} else {
				show_ptp_param_u8(val8_1, false);
				show_ptp_param_u8(val8_2, true);
			}
		} else if (!strcmp(p1, "domain")) {
			gptp_profile_get_domain(v1, &val8_1, &val16_1,
				&val16_2);
			if (count >= 3)
				;
			else
				show_ptp_param_u8(val8_1, true);
		} else if (!strcmp(p1, "ports")) {
			gptp_profile_get_domain(v1, &val8_1, &val16_1,
				&val16_2);
			if (count >= 3)
				;
			else
				show_ptp_param_u16(val16_1, true);
		} else if (!strcmp(p1, "pdelay")) {
			gptp_port_profile_get_delay(v1, &val_1);
			if (count >= 3)
				;
			else
				show_ptp_param_s8(val_1, true);
		} else if (!strcmp(p1, "announce")) {
			gptp_port_profile_get_announce(v1, &val_1, &val8_1);
			if (count >= 3) {
#if 0
				cfg_ptp_param_s8(val_1, v2);
				if (count >= 4)
					cfg_ptp_param_u8(val8_1, v3, NULL, 0, NULL, 0);
#endif
			} else {
				show_ptp_param_s8(val_1, false);
				show_ptp_param_u8(val8_1, true);
			}
		} else if (!strcmp(p1, "sync")) {
			gptp_port_profile_get_sync(v1, &val_1, &val8_1);
			if (count >= 3) {
#if 0
				cfg_ptp_param_s8(val_1, v2);
				if (count >= 4)
					cfg_ptp_param_u8(val8_1, v3, NULL, 0, NULL, 0);
#endif
			} else {
				show_ptp_param_s8(val_1, false);
				show_ptp_param_u8(val8_1, true);
			}
		} else if (!strcmp(p1, "oper")) {
			gptp_port_profile_get_oper(v1, &val_1, &val_2);
			if (count >= 3) {
#if 0
				cfg_ptp_param_s8(val_1, v2);
				if (count >= 4)
					cfg_ptp_param_s8(val_2, v3);
#endif
			} else {
				show_ptp_param_s8(val_1, false);
				show_ptp_param_s8(val_2, true);
			}
		} else if (!strcmp(p1, "thresh")) {
			gptp_port_profile_get_delay_thresh(v1, &val16_1);
			if (count >= 3)
				;
			else
				show_ptp_param_u16(val16_1, true);
		} else if (!strcmp(p1, "latency")) {
			gptp_port_get_latency(v1, &val16_1, &val16_2, &val);
			if (count >= 3) {
#if 0
				cfg_ptp_param_u16(val16_1, v2);
				if (count >= 4)
					cfg_ptp_param_u16(val16_2, v3);
				if (count >= 5)
					cfg_ptp_param_s16(val, v4);
#endif
			} else {
				show_ptp_param_u16(val16_1, false);
				show_ptp_param_u16(val16_2, false);
				show_ptp_param_s16(val, true);
			}
		} else if (!strcmp(p1, "peer_delay")) {
			gptp_port_get_peer_delay(v1, &val16_1);
			if (count >= 3)
				;
			else
				show_ptp_param_u16(val16_1, true);
		} else if (!strcmp(p1, "master")) {
			gptp_port_get_master(v1, &val8_1);
			if (count >= 3)
				;
			else
				show_ptp_param_u8(val8_1, true);
		} else if (!strcmp(p1, "use_delay")) {
			gptp_port_get_use_delay(v1, &val8_1);
			if (count >= 3)
				;
			else
				show_ptp_param_u8(val8_1, true);
		} else if (p1[0] == 'h') {
			printf("\tprofile <index> [0|1|2]"NL);
			printf("\ttransport <index> [0|1]"NL);
			printf("\tsdoid <index> [major] [minor]"NL);
			printf("\tclk <index> [class] [accuracy] [variance]"NL);
			printf("\tprio <index> [prio1] [prio2]"NL);
			printf("\tdomain <index>"NL);
			printf("\tports <index>"NL);
			printf("\tpdelay <index> [-3..2]"NL);
			printf("\tannounce <index> [-3..2] [timeout]"NL);
			printf("\tsync <index> [-4..2] [timeout]"NL);
			printf("\toper <index> [sync] [pdelay]"NL);
			printf("\te2e <index> [0|1]"NL);
			printf("\tp2p <index> [0|1]"NL);
			printf("\tgm_capable <index> [0|1]"NL);
			printf("\ttwo_step <index> [0|1]"NL);
			printf("\tcmlds <index> [0|1]"NL);
			printf("\tthresh <index> [800-65535]"NL);
			printf("\tlatency <port> [rx] [tx] [asym]"NL);
			printf("\tpeer_delay <port> [0-65535]"NL);
			printf("\tmaster <port> [0|1]"NL);
			printf("\tuse_delay <port> [0|1]"NL);
		} else if (p1[0] == 'q') {
			cont = 0;
			break;
		}
	} while (cont);
	return 0;
}

static SOCKET create_sock(char *devname, char *ptp_ip, char *p2p_ip,
	int port, int multi_loop, int family)
{
	SOCKET sockfd;
	struct sockaddr_in servaddr;
	struct sockaddr_in6 servaddr6;
	char *sockopt;
	int reuse = 1;

	bzero(&servaddr, sizeof(servaddr));
	bzero(&servaddr6, sizeof(servaddr6));

	sockfd = Socket(family, SOCK_DGRAM, 0);

	if (AF_INET6 == family) {
		servaddr6.sin6_family = family;
		memcpy(servaddr6.sin6_addr.s6_addr, &in6addr_any,
			sizeof(in6addr_any));
		servaddr6.sin6_port = htons((short) port);
	} else {
		servaddr.sin_family = family;
		servaddr.sin_addr.s_addr = INADDR_ANY;
		servaddr.sin_port = htons((short) port);
	}

	sockopt = (char *) &reuse;
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, sockopt,
			sizeof(reuse)) < 0) {
		printf("reuse\n");
		return -1;
	}
	if (AF_INET6 == family) {
		struct ipv6_mreq mreq;
		u_int iface;
		int hop;
		u_int loop;

		Bind(sockfd, (SA *) &servaddr6, sizeof(servaddr6));
#ifdef IPV6_CLASS
		if (PTP_EVENT_PORT == port) {
			u_int tclass;

			tclass = 0xff;
			sockopt = (char *) &tclass;
			if (setsockopt(sockfd, IPPROTO_IPV6, IPV6_TCLASS,
					sockopt, sizeof(tclass)) < 0) {
				err_ret("no tclass");
			}
		}
#endif

		if (!ptp_ip)
			return sockfd;

		inet_pton(AF_INET6, ptp_ip, &mreq.ipv6mr_multiaddr);
		mreq.ipv6mr_interface = ipv6_interface;
		sockopt = (char *) &mreq;
		if (setsockopt(sockfd, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP,
				sockopt, sizeof(mreq)) < 0) {
			err_ret("add member");
			return -1;
		}
		inet_pton(AF_INET6, p2p_ip, &mreq.ipv6mr_multiaddr);
		mreq.ipv6mr_interface = ipv6_interface;
		sockopt = (char *) &mreq;
		if (setsockopt(sockfd, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP,
				sockopt, sizeof(mreq)) < 0) {
			err_ret("add member to p2p");
			return -1;
		}
		if (ipv6_interface) {
			iface = ipv6_interface;
			sockopt = (char *) &iface;
			if (setsockopt(sockfd, IPPROTO_IPV6, IPV6_MULTICAST_IF,
					sockopt, sizeof(iface)) < 0) {
				err_ret("multi if");
				return -1;
			}
		}
		hop = 1;
		sockopt = (char *) &hop;
		if (setsockopt(sockfd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
				sockopt, sizeof(hop)) < 0) {
			err_ret("hop");
			return -1;
		}
		loop = 0;
		if (multi_loop)
			loop = 1;
		sockopt = (char *) &loop;
		if (setsockopt(sockfd, IPPROTO_IPV6, IPV6_MULTICAST_LOOP,
				sockopt, sizeof(loop)) < 0) {
			err_ret("loop");
			return -1;
		}
	} else {
#ifdef _SYS_SOCKET_H
		struct ip_mreqn mreq;
#else
		struct ip_mreq mreq;
#endif
		u_char hop;
		u_char loop;
		int tos;

		Bind(sockfd, (SA *) &servaddr, sizeof(servaddr));
		if (PTP_EVENT_PORT == port) {
			tos = 0xb8;
			sockopt = (char *) &tos;
			if (setsockopt(sockfd, IPPROTO_IP, IP_TOS,
					sockopt, sizeof(tos)) < 0) {
				err_ret("tos");
				return -1;
			}
		}

		if (!ptp_ip)
			return sockfd;

		memset(&mreq, 0, sizeof(mreq));
		inet_pton(family, ptp_ip, &mreq.imr_multiaddr.s_addr);
#ifdef _SYS_SOCKET_H
		mreq.imr_ifindex = ipv6_interface;
#endif
		sockopt = (char *) &mreq;
		if (setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, sockopt,
				sizeof(mreq)) < 0) {
			err_ret("add member");
			return -1;
		}
		inet_pton(family, p2p_ip, &mreq.imr_multiaddr.s_addr);
#ifdef _SYS_SOCKET_H
		mreq.imr_ifindex = ipv6_interface;
#endif
		sockopt = (char *) &mreq;
		if (setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, sockopt,
				sizeof(mreq)) < 0) {
			err_ret("add member to p2p");
			return -1;
		}
#ifdef _SYS_SOCKET_H
		mreq.imr_ifindex = ipv6_interface;
#endif
		sockopt = (char *) &mreq;
		if (setsockopt(sockfd, IPPROTO_IP, IP_MULTICAST_IF, sockopt,
				sizeof(mreq)) < 0) {
			err_ret("multi if");
			return -1;
		}
		hop = 1;
		sockopt = (char *) &hop;
		if (setsockopt(sockfd, IPPROTO_IP, IP_MULTICAST_TTL, sockopt,
				sizeof(hop)) < 0) {
			err_ret("hop");
			return -1;
		}
		loop = 0;
		if (multi_loop)
			loop = 1;
		sockopt = (char *) &loop;
		if (setsockopt(sockfd, IPPROTO_IP, IP_MULTICAST_LOOP, sockopt,
				sizeof(loop)) < 0) {
			err_ret("loop");
			return -1;
		}
#ifdef _SYS_SOCKET_H
		if (setsockopt(sockfd, SOL_SOCKET, SO_BINDTODEVICE,
		    devname, strlen(devname))) {
			err_ret("bindtodev");
			return -1;
		}
#endif
	}
	return sockfd;
}  /* create_sock */

struct sock_buf {
	struct sockaddr *from;
	u8 *buf;
	int len;
};

static int check_loop(struct sockaddr *sa, int salen)
{
	struct sockaddr_in *addr4;
	struct sockaddr_in6 *addr6;
#ifdef _SYS_SOCKET_H
	struct sockaddr_ll *addr;
#endif

	if (AF_INET6 == sa->sa_family) {
		addr6 = (struct sockaddr_in6 *) sa;
		if (memcmp(&addr6->sin6_addr, host_addr6, 16) == 0)
			return 1;
		if (memcmp(&addr6->sin6_addr.s6_addr[12], host_addr, 4) == 0)
			return 2;
	} else if (AF_INET == sa->sa_family) {
		addr4 = (struct sockaddr_in *) sa;
		if (memcmp(&addr4->sin_addr, host_addr, 4) == 0)
			return 1;
	} else {
#ifdef _SYS_SOCKET_H
		addr = (struct sockaddr_ll *) sa;
		if (addr->sll_ifindex != eth_pdelay_addr.sll_ifindex)
			return 2;
		if (memcmp(addr->sll_addr, hw_addr, ETH_ALEN) == 0)
			return 1;
#endif
	}
	return 0;
}

static int check_dup(struct sock_buf *cur, struct sock_buf *last, int len)
{
	if (cur->len == last->len &&
			memcmp(cur->from, last->from, len) == 0 &&
			memcmp(cur->buf, last->buf, cur->len) == 0)
		return 1;
	return 0;
}

#ifdef _SYS_SOCKET_H
void *

#else
void
#endif
ReceiveTask(void *param)
{
	volatile PTTaskParam pTaskParam;
	u8 *recvbuf;
	SOCKET sockfd;
	SOCKET fd[3];
	struct sock_buf buf[2];
	struct sockaddr_in6 cliaddr[2];
	struct sockaddr_in *addr4;
	struct sockaddr_in6 *addr6;
#ifdef _SYS_SOCKET_H
	struct sockaddr_ll *addr;
#endif
	u8 *raw_addr;
	struct timeval timer;
	fd_set rset;
	fd_set allrset;
	socklen_t len;
	int maxfdp1;
	struct ptp_msg *msg;
	int i;
	char in_addr[80];
	int cur;
	int last;
	int nsel;
	int looped;
	int msglen;
	bool raw_packet = FALSE;

	pTaskParam = (PTTaskParam) param;
	fd[0] = (SOCKET) pTaskParam->multicast;
	fd[1] = (SOCKET) pTaskParam->unicast;
	fd[2] = (SOCKET) pTaskParam->management4;
	len = (MAXBUFFER + 3) & ~3;
	recvbuf = malloc(len * 2);
	buf[0].len = buf[1].len = 0;
	buf[0].buf = recvbuf;
	buf[1].buf = recvbuf + len;
	buf[0].from = (struct sockaddr *) &cliaddr[0];
	buf[1].from = (struct sockaddr *) &cliaddr[1];
	cur = 0;
	last = 1;

#ifdef _WIN32
	SetEvent( pTaskParam->hevTaskComplete );
#endif

	bzero(cliaddr, sizeof(cliaddr));

	FD_ZERO(&allrset);
	FD_SET(fd[0], &allrset);
	if (fd[1])
		FD_SET(fd[1], &allrset);
	if (fd[2])
		FD_SET(fd[2], &allrset);
	maxfdp1 = fd[0];
	if ((int) fd[1] > maxfdp1)
		maxfdp1 = fd[1];
	if ((int) fd[2] > maxfdp1)
		maxfdp1 = fd[2];
	maxfdp1++;
	FOREVER {
		if ( pTaskParam->fTaskStop ) {
			break;
		}

		rset = allrset;
		sockfd = 0;

		timerclear( &timer );
		timer.tv_usec = gWaitDelay * 1000;

		nsel = Select( maxfdp1, &rset, NULL, NULL, &timer );

		/* socket is not readable */
		if (!nsel) {
			if (pTaskParam->fTaskStop) {
				break;
			}
			continue;
		}

		for (i = 0; i < 3; i++) {
			if (nsel <= 0)
				break;

			len = sizeof(struct sockaddr_in6);
			if (FD_ISSET(fd[i], &rset) && sockfd != fd[i])
				sockfd = fd[i];
			else
				continue;

			buf[cur].len = Recvfrom(sockfd, buf[cur].buf,
				MAXBUFFER, 0, buf[cur].from, &len);
			--nsel;
			raw_addr = NULL;
			if (AF_INET6 == buf[cur].from->sa_family) {
				addr6 = (struct sockaddr_in6 *) buf[cur].from;
				inet_ntop(AF_INET6, &addr6->sin6_addr,
					in_addr, sizeof(in_addr));
			} else if (AF_INET == buf[cur].from->sa_family) {
				addr4 = (struct sockaddr_in *) buf[cur].from;
				inet_ntop(AF_INET, &addr4->sin_addr,
					in_addr, sizeof(in_addr));
			} else {
#ifdef _SYS_SOCKET_H
				addr = (struct sockaddr_ll *) buf[cur].from;
				raw_addr = addr->sll_addr;
				raw_packet = TRUE;
				sprintf(in_addr,
					"%02x:%02x:%02x:%02x:%02x:%02x [%d]",
					addr->sll_addr[0], addr->sll_addr[1],
					addr->sll_addr[2], addr->sll_addr[3],
					addr->sll_addr[4], addr->sll_addr[5],
					addr->sll_ifindex);
#endif
			}
			msglen = buf[cur].len;
#ifdef _SYS_SOCKET_H
			if (AF_PACKET == buf[cur].from->sa_family) {
				msg = (struct ptp_msg *) &buf[cur].buf[14];
				msglen -= 14;
			} else
#endif
				msg = (struct ptp_msg *) buf[cur].buf;
			if (msg->hdr.versionPTP < 2) {
				continue;
			}
			looped = check_loop(buf[cur].from, len);
			if (looped) {
				int ignored = 1;

				if (MANAGEMENT_MSG == msg->hdr.messageType &&
						1 == looped)
					ignored = 0;
				if (ignored) {
					continue;
				}
			}
			if (check_dup(&buf[cur], &buf[last], len)) {
				continue;
			} else {
				cur = !cur;
				last = !last;
			}
			if (!stack_running) {
				continue;
			}
#ifdef _SYS_SOCKET_H
			if (ptp_version >= 2) {
				int rc;

				ptp_rx_port = 0;
				rc = get_rx_timestamp(&msg->hdr);
			}
#endif
			mutex_lock(&rx_lock);
			ptp_setup_rx(msg, ptp_rx_port, ptp_rx_sec,
				     ptp_rx_nsec, false, raw_addr, raw_packet,
				     0);
			mutex_unlock(&rx_lock);
		}
	}
	free(recvbuf);
	pTaskParam->fTaskStop = TRUE;

#ifdef _WIN32
	SetEvent( pTaskParam->hevTaskComplete );
#endif

#ifdef _SYS_SOCKET_H
	return NULL;
#endif
}  /* ReceiveTask */

int get_host_info(char *devname, struct ip_info *info)
{
	struct ifi_info *ifi;
	struct ifi_info *ifihead;

	ifihead = get_ifi_info(AF_INET, 1);
	for (ifi = ifihead; ifi != NULL; ifi = ifi->ifi_next) {
		if (!strcmp(devname, ifi->ifi_name)) {
			info->if_idx = ifi->ifi_index;
			if (ifi->ifi_addr != NULL)
				memcpy(&info->addr, ifi->ifi_addr,
					sizeof(struct sockaddr_in));
			memset(info->hwaddr, 0, 8);
			if (ifi->ifi_hlen > 0)
				memcpy(info->hwaddr, ifi->ifi_haddr,
					ifi->ifi_hlen);
			info->plen = 0;
			if (ifi->ifi_addr6 != NULL) {
				memcpy(&info->addr6, ifi->ifi_addr6,
					sizeof(struct sockaddr_in6));
				info->plen = ifi->ifi_plen;
				info->scope = ifi->ifi_scope;
			} else if (ifi->ifi_hlen > 0) {
				u8 *data = (u8 *) &info->addr6.sin6_addr;

				memset(data, 0, 16);
				data[0] = 0xfe;
				data[1] = 0x80;
				memcpy(&data[8], ifi->ifi_haddr, 3);
				data[8] ^= 0x02;
				data[11] = 0xff;
				data[12] = 0xfe;
				memcpy(&data[13], &ifi->ifi_haddr[3], 3);
				info->addr6.sin6_family = AF_INET6;
				info->plen = 64;
				info->scope = 0x20;
			}
			return 1;
		}
	}
	free_ifi_info(ifihead);
	return 0;
}

#ifdef _SYS_SOCKET_H
int get_dev_info(char *devname, struct ip_info *info)
{
	FILE *f;
	int dad_status;
	int count;
	int rc;
	char addr6[40];
	char addr6p[8][5];

	struct ipv6_info *ipv6;
	struct ipv6_info *ipv6head = NULL;

	char path[80];
	char file[40];
	int num[6];
	int found = FALSE;

	/* No IP address. */
	info->addr.sin_family = AF_UNSPEC;

	/* Assume no IPv6 address. */
	info->plen = 0;
	memset(info->hwaddr, 0, 8);

	f = fopen(_PATH_PROCNET_IFINET6, "r");
	if (!f)
		goto get_dev_raw;
	count = 10;
	ipv6head = calloc(count, sizeof(struct ipv6_info));
	ipv6 = ipv6head;
	while (fscanf(f, "%4s%4s%4s%4s%4s%4s%4s%4s %02x %02x %02x %02x %20s\n",
			addr6p[0], addr6p[1], addr6p[2], addr6p[3], addr6p[4],
			addr6p[5], addr6p[6], addr6p[7],
			&ipv6->if_idx, &ipv6->plen, &ipv6->scope, &dad_status,
			ipv6->devname) != EOF) {
		sprintf(addr6, "%s:%s:%s:%s:%s:%s:%s:%s",
			addr6p[0], addr6p[1], addr6p[2], addr6p[3],
			addr6p[4], addr6p[5], addr6p[6], addr6p[7]);
		inet_pton(AF_INET6, addr6,
			(struct sockaddr *) &ipv6->addr.sin6_addr);
		ipv6->addr.sin6_family = AF_INET6;
		if (!strcmp(devname, ipv6->devname)) {
			memcpy(&info->addr6, &ipv6->addr,
				sizeof(struct sockaddr_in6));
			info->if_idx = ipv6->if_idx;
			info->plen = ipv6->plen;
			info->scope = ipv6->scope;
			found = TRUE;
			break;
		}
		ipv6++;
		count--;
		if (1 == count)
			break;
	}
	free(ipv6head);
	fclose(f);

get_dev_raw:
	if (found)
		goto get_dev_addr;

	/* 0x1002 = dev down; 0x1003 = dev up */
	sprintf(path, "%s%s/%s", _PATH_SYSNET_DEV, devname, NETDEV_FLAGS);
	f = fopen(path, "r");
	if (!f)
		goto get_dev_done;
	rc = fscanf(f, "%x", &num[0]);
	fclose(f);

	/* down = link down; up = link up */
	sprintf(path, "%s%s/%s", _PATH_SYSNET_DEV, devname, NETDEV_OPERSTATE);
	f = fopen(path, "r");
	if (!f)
		goto get_dev_addr;
	rc = fscanf(f, "%s", file);
	fclose(f);

	/* Device not running. */
	if (!(num[0] & IFF_UP))
		goto get_dev_done;

get_dev_addr:
	sprintf(path, "%s%s/%s", _PATH_SYSNET_DEV, devname, NETDEV_IFINDEX);
	f = fopen(path, "r");
	if (f) {
		rc = fscanf(f, "%u", &num[0]);
		fclose(f);
		if (!found)
			info->if_idx = num[0];
		else if (info->if_idx != num[0])
			printf(" ? %d %d\n", info->if_idx, num[0]);
	}

	found = TRUE;

	sprintf(path, "%s%s/%s", _PATH_SYSNET_DEV, devname, NETDEV_ADDRESS);
	f = fopen(path, "r");
	if (!f)
		goto get_dev_done;
	rc = fscanf(f, "%x:%x:%x:%x:%x:%x",
		&num[0], &num[1], &num[2], &num[3], &num[4], &num[5]);
	fclose(f);
	for (count = 0; count < 6; count++)
		info->hwaddr[count] = (u8) num[count];

get_dev_done:
	return found;
}

static int get_vlan_dev(char *devname, int vlan)
{
	FILE *f;
	int rc;
	char path[80];
	char file[40];
	int num[6];

	sprintf(path, "%s%s/sw/%s", _PATH_SYSNET_DEV, devname, "dev_start");
	f = fopen(path, "r");
	if (!f)
		goto get_vlan_done;
	rc = fscanf(f, "%u", &num[0]);
	fclose(f);
	if (num[0])
		goto next;

	sprintf(path, "%s%s/sw/%s", _PATH_SYSNET_DEV, devname, "vlan_start");
	f = fopen(path, "r");
	if (!f)
		goto get_vlan_done;
	rc = fscanf(f, "%u", &num[0]);
	fclose(f);
	if (num[0])
		num[0]++;

next:
	sprintf(path, "%s%s/sw/%s", _PATH_SYSNET_DEV, devname, "ports");
	f = fopen(path, "r");
	if (!f)
		goto get_vlan_done;
	rc = fscanf(f, "%u", &num[1]);
	fclose(f);

	num[2] = 0;
	sprintf(path, "%s%s/sw/%s", _PATH_SYSNET_DEV, devname, "host_port");
	f = fopen(path, "r");
	if (f) {
		rc = fscanf(f, "%u", &num[2]);
		fclose(f);
	}

	if (num[2])
		--num[1];
	if (num[0] && num[0] <= vlan && vlan < num[0] + num[1])
		return TRUE;

get_vlan_done:
	return FALSE;
}

static void add_multi(SOCKET sockfd, char *local_if)
{
	struct ifreq ifr;
	int rc;

	strcpy(ifr.ifr_name, local_if);
	ifr.ifr_hwaddr.sa_family = AF_UNSPEC;
	memcpy(ifr.ifr_hwaddr.sa_data, eth_pdelay, ETH_ALEN);
	rc = ioctl(sockfd, SIOCADDMULTI, &ifr);
	if (eth_others[0]) {
		memcpy(ifr.ifr_hwaddr.sa_data, eth_others, ETH_ALEN);
		rc = ioctl(sockfd, SIOCADDMULTI, &ifr);
	}
}

static void del_multi(SOCKET sockfd, char *local_if)
{
	struct ifreq ifr;
	int rc;

	strcpy(ifr.ifr_name, local_if);
	ifr.ifr_hwaddr.sa_family = AF_UNSPEC;
	memcpy(ifr.ifr_hwaddr.sa_data, eth_pdelay, ETH_ALEN);
	rc = ioctl(sockfd, SIOCDELMULTI, &ifr);
	if (eth_others[0]) {
		memcpy(ifr.ifr_hwaddr.sa_data, eth_others, ETH_ALEN);
		rc = ioctl(sockfd, SIOCDELMULTI, &ifr);
	}
}

static SOCKET create_raw(struct ip_info *info, char *dest)
{
	SOCKET sockfd;
	struct ethhdr *eh;
	int addr[ETH_ALEN];
	int cnt;

	sockfd = Socket(AF_PACKET, SOCK_RAW, htons(ptp_proto));
	if (sockfd < 0)
		return sockfd;

	cnt = sscanf(dest, "%x:%x:%x:%x:%x:%x",
		&addr[0], &addr[1], &addr[2], &addr[3], &addr[4], &addr[5]);
	eth_pdelay_addr.sll_family = PF_PACKET;
	eth_pdelay_addr.sll_protocol = htons(ptp_proto);
	eth_pdelay_addr.sll_ifindex = info->if_idx;
	eth_pdelay_addr.sll_hatype = ARPHRD_ETHER;
	eth_pdelay_addr.sll_halen = ETH_ALEN;
	if (ETH_ALEN == cnt) {
		eth_pdelay_addr.sll_pkttype = PACKET_OTHERHOST;
		eth_pdelay_addr.sll_addr[0] = (u8) addr[0];
		eth_pdelay_addr.sll_addr[1] = (u8) addr[1];
		eth_pdelay_addr.sll_addr[2] = (u8) addr[2];
		eth_pdelay_addr.sll_addr[3] = (u8) addr[3];
		eth_pdelay_addr.sll_addr[4] = (u8) addr[4];
		eth_pdelay_addr.sll_addr[5] = (u8) addr[5];
	} else {
		eth_pdelay_addr.sll_pkttype = PACKET_MULTICAST;
		memcpy(eth_pdelay_addr.sll_addr, eth_pdelay, ETH_ALEN);
	}
	eth_pdelay_addr.sll_addr[6] = 0x00;
	eth_pdelay_addr.sll_addr[7] = 0x00;
	eth_others_addr.sll_family = PF_PACKET;
	eth_others_addr.sll_protocol = htons(ptp_proto);
	eth_others_addr.sll_ifindex = info->if_idx;
	eth_others_addr.sll_hatype = ARPHRD_ETHER;
	eth_others_addr.sll_halen = ETH_ALEN;
	if (ETH_ALEN == cnt) {
		eth_others_addr.sll_pkttype = PACKET_OTHERHOST;
		eth_others_addr.sll_addr[0] = (u8) addr[0];
		eth_others_addr.sll_addr[1] = (u8) addr[1];
		eth_others_addr.sll_addr[2] = (u8) addr[2];
		eth_others_addr.sll_addr[3] = (u8) addr[3];
		eth_others_addr.sll_addr[4] = (u8) addr[4];
		eth_others_addr.sll_addr[5] = (u8) addr[5];
	} else {
		eth_others_addr.sll_pkttype = PACKET_MULTICAST;
		if (eth_others[0])
			memcpy(eth_others_addr.sll_addr, eth_others, ETH_ALEN);
		else
			memcpy(eth_others_addr.sll_addr, eth_pdelay, ETH_ALEN);
	}
	eth_others_addr.sll_addr[6] = 0x00;
	eth_others_addr.sll_addr[7] = 0x00;

	eth_pdelay_buf = malloc(4018);
	eth_others_buf = malloc(4018);
	memcpy(eth_pdelay_buf, eth_pdelay_addr.sll_addr, ETH_ALEN);
	memcpy(&eth_pdelay_buf[ETH_ALEN], info->hwaddr, ETH_ALEN);
	eh = (struct ethhdr *) eth_pdelay_buf;
	eh->h_proto = htons(ptp_proto);
	memcpy(eth_others_buf, eth_others_addr.sll_addr, ETH_ALEN);
	memcpy(&eth_others_buf[ETH_ALEN], info->hwaddr, ETH_ALEN);
	eh = (struct ethhdr *) eth_others_buf;
	eh->h_proto = htons(ptp_proto);

	return sockfd;
}
#endif

static void handle_int_quit_term(int s)
{
	printf("\nUse comand 'q' to quit.\n");
}

int handle_term_signals(void)
{
	if (SIG_ERR == signal(SIGINT, handle_int_quit_term)) {
		fprintf(stderr, "cannot handle SIGINT\n");
		return -1;
	}
	if (SIG_ERR == signal(SIGQUIT, handle_int_quit_term)) {
		fprintf(stderr, "cannot handle SIGQUIT\n");
		return -1;
	}
	if (SIG_ERR == signal(SIGTERM, handle_int_quit_term)) {
		fprintf(stderr, "cannot handle SIGTERM\n");
		return -1;
	}
	return 0;
}

static pthread_t sock_tid[4];
static TTaskParam sock_param[4];
static int sock_task_cnt;

static void exit_sock(char *eth_dev)
{
	void *status;
	int i;

#ifdef _SYS_SOCKET_H
#ifdef USE_NET_IOCTL
	if (ptp_hw) {
		int rc;

		rc = ptp_dev_exit(&ptpdev);
		if (rc)
			print_err(rc);
	}
#endif
#endif
	for (i = 0; i < sock_task_cnt; i++) {
		sock_param[i].fTaskStop = TRUE;
	}

	// wait for task to end
	for (i = 0; i < sock_task_cnt; i++) {

#ifdef _SYS_SOCKET_H
		Pthread_join( sock_tid[i], &status );

#elif defined( _WIN32 )
		rc = WaitForSingleObject( param[i].hevTaskComplete, INFINITE );
		if ( WAIT_FAILED == rc ) {

		}
		ResetEvent( param[i].hevTaskComplete );
		Sleep( 1 );

#else
		DelayMillisec( 200 );
#endif
	}

#ifdef _SYS_SOCKET_H
	if (eth_fd > 0) {
		del_multi(eth_fd, eth_dev);
		free(eth_pdelay_buf);
		free(eth_others_buf);
		closesocket(eth_fd);
	}
	if (eth_avtpdu > 0) {
		del_multi(eth_avtpdu, eth_dev);
		free(eth_avtpdu_buf);
		closesocket(eth_avtpdu);
	}
#endif
	closesocket(event_fd);
	closesocket(general_fd);
	if (uni_event_fd != event_fd)
		closesocket(uni_event_fd);
	if (uni_general_fd != general_fd)
		closesocket(uni_general_fd);
	if (management4_fd != general_fd)
		closesocket(management4_fd);
}

static int init_sock(char *eth_dev, int family, u8 *hwaddr)
{
	char dest_ip[40];
	char host_ip4[40];
	char host_ip6[40];
	char *host_ip;
	char *ptp_ip = NULL;
	char *p2p_ip = NULL;
	struct ip_info info;
	int i;
	int multi_loop = 0;
	int unicast_sock = 0;
	int vlan = 0;

	i = snprintf(devname, sizeof(devname), "%s", eth_dev);
	if (i > sizeof(devname) - 1)
		printf("eth_dev too long\n");
	host_ip = strchr(devname, '.');
	if (host_ip != NULL)
		*host_ip = 0;
#ifdef _SYS_SOCKET_H
#ifdef USE_NET_IOCTL
	i = snprintf(ptpdev.name, sizeof(ptpdev.name), "%s", devname);
	if (i > sizeof(devname) - 1)
		printf("devname too long\n");
#endif
#endif
	if (host_ip != NULL) {
		++host_ip;
		vlan = atoi(host_ip);
	}

	if (get_host_info(eth_dev, &info)) {
		memcpy(host_addr, &info.addr.sin_addr, 4);
		inet_ntop(AF_INET, &info.addr.sin_addr,
			host_ip4, sizeof(host_ip4));
#if 0
		printf("%s\n", host_ip4);
#endif
		ipv6_interface = info.if_idx;
		if (info.plen) {
			inet_ntop(AF_INET6, &info.addr6.sin6_addr,
				host_ip6, sizeof(host_ip6));
#if 0
			printf("%s\n", host_ip6);
#endif
			memcpy(host_addr6, &info.addr6.sin6_addr, 16);
			ipv6_interface = info.if_idx;
		}
#ifdef _SYS_SOCKET_H
		memcpy(hw_addr, info.hwaddr, ETH_ALEN);
#endif
		memcpy(&selfClockIdentity.addr[0], info.hwaddr, 3);
		selfClockIdentity.addr[3] = 0xFF;
		selfClockIdentity.addr[4] = 0xFE;
		memcpy(&selfClockIdentity.addr[5], &info.hwaddr[3], 3);
#ifdef _SYS_SOCKET_H
	} else if (get_dev_info(eth_dev, &info)) {
		ipv6_interface = info.if_idx;
		if (info.plen) {
			inet_ntop(AF_INET6, &info.addr6.sin6_addr,
				host_ip6, sizeof(host_ip6));
#if 0
			printf("%s\n", host_ip6);
#endif
			memcpy(host_addr6, &info.addr6.sin6_addr, 16);
			ipv6_interface = info.if_idx;
		}
		memcpy(hw_addr, info.hwaddr, ETH_ALEN);
		memcpy(&selfClockIdentity.addr[0], info.hwaddr, 3);
		selfClockIdentity.addr[3] = 0xFF;
		selfClockIdentity.addr[4] = 0xFE;
		memcpy(&selfClockIdentity.addr[5], &info.hwaddr[3], 3);

		/* Not using "eth0.100" device. */
		if (!strcmp(devname, eth_dev))
			strcpy(devname, "br0");
		if (strcmp(devname, eth_dev)) {
			struct ip_info parent_info;

			if (get_host_info(devname, &parent_info)) {
				memcpy(host_addr, &parent_info.addr.sin_addr,
				       4);
				inet_ntop(AF_INET, &parent_info.addr.sin_addr,
					host_ip4, sizeof(host_ip4));
				printf("use %s\n", host_ip4);
			} else if (AF_INET == family && ptp_unicast) {
				/* Need IP address to send unicast. */
				ptp_unicast = 0;
			}
		}
#endif
	} else {
		printf("cannot locate IP address\n");
		exit(1);
	}

	ip_family = family;
	if (AF_INET6 == family) {
		host_ip = host_ip6;
		event_addr6.sin6_family = family;
		event_addr6.sin6_port = htons(PTP_EVENT_PORT);

		general_addr6.sin6_family = family;
		general_addr6.sin6_port = htons(PTP_GENERAL_PORT);

		p2p_event_addr6.sin6_family = family;
		p2p_event_addr6.sin6_port = htons(PTP_EVENT_PORT);

		p2p_general_addr6.sin6_family = family;
		p2p_general_addr6.sin6_port = htons(PTP_GENERAL_PORT);
		if (ptp_unicast) {
			inet_pton(family, dest_ip, &event_addr6.sin6_addr);
			inet_pton(family, dest_ip, &general_addr6.sin6_addr);
			inet_pton(family, dest_ip, &p2p_event_addr6.sin6_addr);
			inet_pton(family, dest_ip,
				&p2p_general_addr6.sin6_addr);
		} else {
			ptp_ip = PTP_ip_addr6;
			p2p_ip = P2P_ip_addr6;

			inet_pton(family, ptp_ip, &event_addr6.sin6_addr);
			inet_pton(family, ptp_ip, &general_addr6.sin6_addr);
			inet_pton(family, p2p_ip, &p2p_event_addr6.sin6_addr);
			inet_pton(family, p2p_ip,
				&p2p_general_addr6.sin6_addr);
		}
	} else {
		family = AF_INET;
		host_ip = host_ip4;
		event_addr.sin_family = family;
		event_addr.sin_port = htons(PTP_EVENT_PORT);

		general_addr.sin_family = family;
		general_addr.sin_port = htons(PTP_GENERAL_PORT);

		p2p_event_addr.sin_family = family;
		p2p_event_addr.sin_port = htons(PTP_EVENT_PORT);

		p2p_general_addr.sin_family = family;
		p2p_general_addr.sin_port = htons(PTP_GENERAL_PORT);
		if (ptp_unicast && AF_INET == ip_family) {
			inet_pton(family, dest_ip,
				&event_addr.sin_addr.s_addr);
			inet_pton(family, dest_ip,
				&general_addr.sin_addr.s_addr);
			inet_pton(family, dest_ip,
				&p2p_event_addr.sin_addr.s_addr);
			inet_pton(family, dest_ip,
				&p2p_general_addr.sin_addr.s_addr);
		} else {
			ptp_ip = PTP_ip_addr;
			p2p_ip = P2P_ip_addr;

			inet_pton(family, ptp_ip, &event_addr.sin_addr.s_addr);
			inet_pton(family, ptp_ip,
				&general_addr.sin_addr.s_addr);
			inet_pton(family, p2p_ip,
				&p2p_event_addr.sin_addr.s_addr);
			inet_pton(family, p2p_ip,
				&p2p_general_addr.sin_addr.s_addr);
		}
	}
	management_addr.sin_family = AF_INET;
	management_addr.sin_port = htons(PTP_GENERAL_PORT);
	inet_pton(AF_INET, PTP_ip_addr, &management_addr.sin_addr);
	management_addr6.sin6_family = AF_INET6;
	management_addr6.sin6_port = htons(PTP_GENERAL_PORT);
	inet_pton(AF_INET6, PTP_ip_addr6, &management_addr6.sin6_addr);

	event_fd = create_sock(eth_dev, ptp_ip, p2p_ip,
		PTP_EVENT_PORT, 0, family);
	if (event_fd < 0) {
		printf("Cannot create socket\n");
		return 1;
	}
	general_fd = create_sock(eth_dev, ptp_ip, p2p_ip,
		PTP_GENERAL_PORT, multi_loop, family);
	if (general_fd < 0) {
		printf("Cannot create socket\n");
		return 1;
	}
	sock_task_cnt = 2;
	uni_event_fd = event_fd;
	uni_general_fd = general_fd;
	if (!unicast_sock)
		ptp_ip = NULL;
	if (ptp_ip) {
		uni_event_fd = create_sock(eth_dev, NULL, NULL,
			PTP_EVENT_PORT, 0, family);
		if (uni_event_fd < 0) {
			printf("Cannot create socket\n");
			return 1;
		}
		uni_general_fd = create_sock(eth_dev, NULL, NULL,
			PTP_GENERAL_PORT, 0, family);
		if (uni_general_fd < 0) {
			printf("Cannot create socket\n");
			return 1;
		}
	}
	management4_fd = general_fd;
	if (AF_INET6 == ip_family) {
		management4_fd = create_sock(eth_dev, PTP_ip_addr, P2P_ip_addr,
			PTP_GENERAL_PORT, multi_loop, AF_INET);
		if (management4_fd < 0) {
			printf("Cannot create socket\n");
		}
	}
	sptr = &event_fd;

#ifdef _SYS_SOCKET_H
#ifdef USE_NET_IOCTL
	ptpdev.sock = event_fd;
#endif
	dev[1].fd = &ptpdev;

	eth_fd = -1;
	if (AF_PACKET == ip_family) {
		eth_fd = create_raw(&info, dest_ip);
		if (eth_fd < 0) {
			printf("Cannot create socket\n");
			return 1;
		}
		add_multi(eth_fd, eth_dev);
		sock_task_cnt++;
		sptr = &eth_fd;
#ifdef USE_NET_IOCTL
		ptpdev.sock = eth_fd;
#endif
		eth_avtpdu = create_avtpdu(&info);
		if (eth_avtpdu > 0)
			add_multi(eth_avtpdu, eth_dev);
	}
#endif

	for (i = 0; i < sock_task_cnt; i++) {
		sock_param[i].fTaskStop = FALSE;
		switch (i) {
		case 0:
			sock_param[i].multicast = event_fd;
			sock_param[i].unicast = uni_event_fd;
			sock_param[i].management4 = 0;
			sock_param[i].msgs = event_msgs;
			break;
		case 1:
			sock_param[i].multicast = general_fd;
			sock_param[i].unicast = uni_general_fd;
			sock_param[i].management4 = management4_fd;
			sock_param[i].msgs = general_msgs;
			break;
		case 2:
			sock_param[i].multicast = eth_fd;
			sock_param[i].unicast = 0;
			sock_param[i].management4 = 0;
			break;
		}

#ifdef _SYS_SOCKET_H
		Pthread_create(&sock_tid[i], NULL, ReceiveTask, &sock_param[i]);

#elif defined( _WIN32 )
		param[i].hevTaskComplete =
			CreateEvent( NULL, TRUE, FALSE, NULL );
		if ( NULL == param[i].hevTaskComplete ) {

		}
		_beginthread( ReceiveTask, 4096, &param[i] );

		// wait for task to start
		rc = WaitForSingleObject( param[i].hevTaskComplete, INFINITE );
		if ( WAIT_FAILED == rc ) {

		}
		ResetEvent( param[i].hevTaskComplete );
		Sleep( 1 );
#endif
	}

	if ( !sock_param[0].fTaskStop ) {
		int rc;

#ifdef _SYS_SOCKET_H
#ifdef USE_NET_IOCTL
		int capability;

		ptp_hw = 0;

		capability = PTP_KNOW_ABOUT_MULT_PORTS;
		if (strcmp(devname, eth_dev) && get_vlan_dev(devname, vlan)) {
			capability |= PTP_HAVE_MULT_DEVICES;
		}
		rc = ptp_dev_init(&ptpdev, capability,
			&ptp_drift, &ptp_version, &ptp_ports, &ptp_host_port);
		if (!rc) {
			printf("drift=%d version=%d ports=%d\n",
				ptp_drift, ptp_version, ptp_ports);
			ptp_hw = 1;
		}
#endif
#endif
		handle_term_signals();
	}

	memcpy(hwaddr, info.hwaddr, ETH_ALEN);
	return 0;
}

