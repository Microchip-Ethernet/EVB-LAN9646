/*
    Copyright (C) 2020-2026 Microchip Technology Inc. and its
    subsidiaries (Microchip).  All rights reserved.

    You are permitted to use the software and its derivatives with Microchip
    products. See the license agreement accompanying this software, if any,
    for additional info regarding your rights and obligations.

    SOFTWARE AND DOCUMENTATION ARE PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
    KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION, ANY
    WARRANTY OF MERCHANTABILITY, TITLE, NON-INFRINGEMENT AND FITNESS FOR A
    PARTICULAR PURPOSE. IN NO EVENT SHALL MICROCHIP, OR ITS LICENSORS BE
    LIABLE OR OBLIGATED UNDER CONTRACT, NEGLIGENCE, STRICT LIABILITY,
    CONTRIBUTION, BREACH OF WARRANTY, OR OTHER LEGAL EQUITABLE THEORY FOR
    ANY DIRECT OR INDIRECT DAMAGES OR EXPENSES INCLUDING BUT NOT LIMITED TO
    ANY INCIDENTAL, SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES, OR OTHER
    SIMILAR COSTS. TO THE FULLEST EXTENT ALLOWED BY LAW, MICROCHIP AND ITS
    LICENSORS LIABILITY WILL NOT EXCEED THE AMOUNT OF FEES, IF ANY, THAT YOU
    PAID DIRECTLY TO MICROCHIP TO USE THIS SOFTWARE. MICROCHIP PROVIDES THIS
    SOFTWARE CONDITIONALLY UPON YOUR ACCEPTANCE OF THESE TERMS.
*/

#include "os.h"

#ifdef LINUX_PTP
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
#endif
#else
#include "config_store/config_store_init.h"
#endif

#ifdef USE_CYCLONETCP
typedef Socket *sockptr;

#define closesocket socketClose
#endif

#ifdef USE_FREERTOS_PLUS
#include "FreeRTOS_IP.h"
#include "FreeRTOS_UDP_IP.h"
#include "FreeRTOS_Sockets.h"

#define usleep(delay) \
    {volatile uint n = delay * 4; while (n > 0) n--; }

#define sleep(delay) \
    {volatile uint n = delay * 4000; while (n > 0) n--; }


struct tail_tag_info {
    uint sec;
    uint nsec;
    uint16_t port;
    uint8_t queue;
};

typedef Socket_t sockptr;

#define closesocket FreeRTOS_closesocket
#endif

#ifdef LINUX_PTP
typedef SOCKET sockptr;

#if defined(__GNUC__)
#ifndef __packed
#define __packed __attribute__((packed))
#endif
#endif

#define USE_DEV_IOCTL
#define USE_TIMESTAMP_OPER

#include "lan937x_ptp_api.c"
#else
#include "lan937x_ptp_api.h"


#define PTP_PROTO       0x88F7

struct dev_info {
    struct file_dev_info *fd;
};

static struct dev_info dev[2];
#endif

static u8 ptp_tsi_units;
static u8 ptp_tsi_events;
static u8 ptp_tsi_extra;

static int ptp_drift;
static u8 ptp_version;
static u8 ptp_ports;
static u8 ptp_host_port;


static char u1_str[40];
static char u2_str[40];
static char u3_str[40];
static char u4_str[40];


#ifdef LINUX_PTP
#define DEBUG_MSG

static void inc_mac_addr(char *addr, u8 inc)
{
    u8 prev;

    prev = addr[2];
    addr[2] += inc;
    if (addr[2] < prev) {
        prev = addr[1];
        addr[1] += 1;
        if (addr[1] < prev)
            addr[0] += 1;
    }
}

static int stack_running;
#endif

#include "ptp_dbg.h"
#include "ptp_config.c"
#include "ptp_ptu.c"
#include "ptp_filter.c"
#include "ptp_servo.c"
#include "ptp_msg.c"
#include "ptp_bmca.c"
#include "ptp_protocol.c"
#ifdef LINUX_PTP
#include "ptp_sock.c"
#endif
#include "ptp_sync.c"
#include "ptp_port.c"
#include "ptp_clock.c"
#include "ptp_manage.c"
#include "ptp_signal.c"
#include "ptp_forward.c"

static struct ptp_clock *ptp_clock_ptr[MAX_PTP_CLOCKS];
static u8 ptp_clock_cnt;
static u8 ptp_clock_domains[3] = {0, 5, 3};
static u16 ptp_clock_domain_ports[3];
static u16 ptp_clock_vid[3] = {0, 0, 0};

static char *ptp_ip = "204.0.1.129";
static char *p2p_ip = "204.0.0.127";
static char *host_ip = "192.168.0.20";

#ifdef USE_CYCLONETCP
static const Ipv4Addr PTP_IPV4_ADDR = IPV4_ADDR(224, 0, 1, 129);
static const Ipv4Addr P2P_IPV4_ADDR = IPV4_ADDR(224, 0, 0, 107);

static const Ipv6Addr PTP_IPV6_ADDR = IPV6_ADDR(0xFF02, 0, 0, 0, 0, 0, 0, 0x0181);
static const Ipv6Addr P2P_IPV6_ADDR = IPV6_ADDR(0xFF02, 0, 0, 0, 0, 0, 0, 0x006B);
#endif

#ifdef USE_FREERTOS_PLUS
static const u8 PTP_IPV4_ADDR[4] = { 224, 0, 1, 129 };
static const u8 P2P_IPV4_ADDR[4] = { 224, 0, 0, 107 };
#endif

#ifndef LINUX_PTP
static int ptp_app_init(void)
{
    int cap = PTP_KNOW_ABOUT_MULT_PORTS;

    dev[0].fd = ptp_dev_open(0);
    if (!dev[0].fd) {
        return -1;
    }
    dev[1].fd = ptp_dev_open(1);
    if (!dev[1].fd) {
        file_dev_release(dev[0].fd);
        return - 1;
    }

    if (ptp_dev_init(dev[0].fd, cap, &ptp_drift, &ptp_version, &ptp_ports,
        &ptp_host_port))
        return 1;
    ptp_tsi_units = 12;
    ptp_tsi_events = 2;
    ptp_tsi_extra = 6;
    if (ptp_version >= 2) {
        int rc;

        rc = get_rx_event_info(dev[0].fd,
            &ptp_tsi_units, &ptp_tsi_events, &ptp_tsi_extra);
        if (rc) {
            ptp_tsi_units = 12;
            ptp_tsi_events = 2;
            ptp_tsi_extra = 6;
        }
    }

    return 0;
}

static void ptp_app_cleanup(void)
{
    if (dev[0].fd) {
        file_dev_release(dev[0].fd);
        dev[0].fd = NULL;
    }
    if (dev[1].fd) {
        file_dev_release(dev[1].fd);
        dev[1].fd = NULL;
    }
}

static int ptp_app_exit(void)
{
    int rc = 0;

    if (dev[0].fd) {
        rc = ptp_dev_exit(dev[0].fd);
    }
    return rc;
}

static char *PTP_ip_addr = "224.0.1.129";
static char *P2P_ip_addr = "224.0.0.107";
static char *PTP_ip_addr6_const = "ff02::0181";
static char *P2P_ip_addr6_const = "ff02::6b";
static char PTP_ip_addr6[20];
static char P2P_ip_addr6[20];

#define PTP_EVENT_PORT          319
#define PTP_GENERAL_PORT        320

static int ip_family;
static int dbg_rcv = 0;
static int ptp_proto = PTP_PROTO;

static sockptr event_fd;
static sockptr general_fd;
static sockptr eth_fd;
#endif
static u16 sock_vlan[3];

#ifndef LINUX_PTP
static struct file_dev_info *ptpdev;

static u8 *eth_pdelay_buf;
static u8 *eth_others_buf;
static u8 *eth_unicast_buf;

static u8 eth_pdelay[] = { 0x01, 0x80, 0xC2, 0x00, 0x00, 0x0E };
static u8 eth_others[] = { 0x01, 0x1B, 0x19, 0x00, 0x00, 0x00 };
#endif

static u8 ptp_tx_queue;

static int ptp_rx_port;
static u32 ptp_rx_sec;
static u32 ptp_rx_nsec;

static int ptp_tx_port;
static u32 ptp_tx_sec;
static u32 ptp_tx_nsec;

#ifndef LINUX_PTP
struct ip_info {
    u8 hwaddr[8];
    int plen;
    int scope;
    int if_idx;
};

static struct ip_info hw_info;

static void inc_mac_addr(char *addr, u8 inc)
{
    u8 prev;

    prev = addr[2];
    addr[2] += inc;
    if (addr[2] < prev) {
        prev = addr[1];
        addr[1] += 1;
        if (addr[1] < prev)
            addr[0] += 1;
    }
}

static
void send_msg(struct ptp_message *m, int family, u16 vid, int len, u8 index,
              u8* dest)
{
    struct ptp_msg *msg = m->msg;
    sockptr sockfd = eth_fd;
#ifdef USE_CYCLONETCP
    IpAddr ip_addr;
    size_t written;
#endif
#ifdef USE_FREERTOS_PLUS
    struct freertos_sockaddr ip_addr;
#endif
    int err;
    char *buf = (char *) msg;
    int ptp_start = 0;

    if (!len)
        len = ntohs(msg->hdr.messageLength);
    if (AF_INET6 == family) {
        buf[len] = 0x15;
        buf[len + 1] = 0x88;
        len += 2;
    }

#ifdef USE_CYCLONETCP
    if (AF_INET == family)
        ip_addr.length = sizeof(Ipv4Addr);
    else if (AF_INET6 == family)
        ip_addr.length = sizeof(Ipv6Addr);
#endif
    ptp_tx_queue = 7;
    if (AF_PACKET == family) {
        sockfd = eth_fd;
        switch (msg->hdr.messageType) {
        case PDELAY_REQ_MSG:
        case PDELAY_RESP_MSG:
        case PDELAY_RESP_FOLLOW_UP_MSG:
            buf = (char *) eth_pdelay_buf;
            break;
        default:
            if (msg->hdr.transportSpecific)
                buf = (char *) eth_pdelay_buf;
            else
                buf = (char *) eth_others_buf;
            break;
        }
        if (msg->hdr.flagField.flag.unicastFlag && dest) {
            buf = (char *) eth_unicast_buf;
            memcpy(buf, dest, ETH_ALEN);
        }
        memcpy(&buf[ETH_ALEN], hw_info.hwaddr, ETH_ALEN);
        inc_mac_addr(&buf[ETH_ALEN + 3], index);

        ptp_start = 14;
        memcpy(m->payload, buf, ptp_start);
        buf = (char *) m->payload;
        len += ptp_start;
    } else
    switch (msg->hdr.messageType) {
    case SYNC_MSG:
    case DELAY_REQ_MSG:
        sockfd = event_fd;
#ifdef USE_CYCLONETCP
        if (AF_INET6 == family)
            ipv6CopyAddr(&ip_addr.ipv6Addr, &PTP_IPV6_ADDR);
        else
            ipv4CopyAddr(&ip_addr.ipv4Addr, &PTP_IPV4_ADDR);
#endif
#ifdef USE_FREERTOS_PLUS
        memcpy(&ip_addr.sin_addr, PTP_IPV4_ADDR, 4);
        ip_addr.sin_port = FreeRTOS_htons(PTP_EVENT_PORT);
#endif
        break;
    case PDELAY_REQ_MSG:
    case PDELAY_RESP_MSG:
        sockfd = event_fd;
#ifdef USE_CYCLONETCP
        if (AF_INET6 == family)
            ipv6CopyAddr(&ip_addr.ipv6Addr, &P2P_IPV6_ADDR);
        else
            ipv4CopyAddr(&ip_addr.ipv4Addr, &P2P_IPV4_ADDR);
#endif
#ifdef USE_FREERTOS_PLUS
        memcpy(&ip_addr.sin_addr, P2P_IPV4_ADDR, 4);
        ip_addr.sin_port = FreeRTOS_htons(PTP_EVENT_PORT);
#endif
        break;
    case PDELAY_RESP_FOLLOW_UP_MSG:
        sockfd = general_fd;
#ifdef USE_CYCLONETCP
        if (AF_INET6 == family)
            ipv6CopyAddr(&ip_addr.ipv6Addr, &P2P_IPV6_ADDR);
        else
            ipv4CopyAddr(&ip_addr.ipv4Addr, &P2P_IPV4_ADDR);
#endif
#ifdef USE_FREERTOS_PLUS
        memcpy(&ip_addr.sin_addr, P2P_IPV4_ADDR, 4);
        ip_addr.sin_port = FreeRTOS_htons(PTP_GENERAL_PORT);
#endif
        break;
    case MANAGEMENT_MSG:
        sockfd = general_fd;
#ifdef USE_CYCLONETCP
        if (AF_INET6 == family)
            ipv6CopyAddr(&ip_addr.ipv6Addr, &PTP_IPV6_ADDR);
        else
            ipv4CopyAddr(&ip_addr.ipv4Addr, &PTP_IPV4_ADDR);
#endif
#ifdef USE_FREERTOS_PLUS
        memcpy(&ip_addr.sin_addr, PTP_IPV4_ADDR, 4);
        ip_addr.sin_port = FreeRTOS_htons(PTP_GENERAL_PORT);
#endif
        ptp_tx_queue = 0;
        break;
    default:
        sockfd = general_fd;
#ifdef USE_CYCLONETCP
        if (AF_INET6 == family)
            ipv6CopyAddr(&ip_addr.ipv6Addr, &PTP_IPV6_ADDR);
        else
            ipv4CopyAddr(&ip_addr.ipv4Addr, &PTP_IPV4_ADDR);
#endif
#ifdef USE_FREERTOS_PLUS
        memcpy(&ip_addr.sin_addr, PTP_IPV4_ADDR, 4);
        ip_addr.sin_port = FreeRTOS_htons(PTP_GENERAL_PORT);
#endif
        ptp_tx_queue = 0;
        break;
    }
#ifdef USE_CYCLONETCP
    if (sockfd->interface) {
        sockfd->interface->tx_port = ptp_tx_port;
        sockfd->interface->tx_queue = ptp_tx_queue;
        sockfd->interface->tx_sec = ptp_tx_sec;
        sockfd->interface->tx_nsec = ptp_tx_nsec;
    }
#endif
#ifdef USE_FREERTOS_PLUS
    do {
        struct tail_tag_info tail_tag;
        u16 *vlan;

        tail_tag.port = ptp_tx_port;
        tail_tag.queue = ptp_tx_queue;
        tail_tag.sec = ptp_tx_sec;
        tail_tag.nsec = ptp_tx_nsec;
        err = FreeRTOS_setsockopt(sockfd, 0, FREERTOS_SO_TAIL_TAG,
                                  &tail_tag, sizeof(tail_tag));
        if (sockfd == eth_fd)
            vlan = &sock_vlan[0];
        else if (sockfd == event_fd)
            vlan = &sock_vlan[1];
        else
            vlan = &sock_vlan[2];
        if (vid != *vlan) {
            *vlan = vid;
            err = FreeRTOS_setsockopt(sockfd, 0, FREERTOS_SO_VLAN,
                                      &vid, sizeof(vid));
        }
    } while (0);
#endif
    err = set_tail_tag(ptpdev, buf, msg, len, ptp_tx_port, ptp_tx_queue,
                       NULL, NULL);
    (void ) err;
#ifdef USE_CYCLONETCP
    if (AF_PACKET == family)
        err = socketSendTo(sockfd, NULL, 0, buf, len, &written, 0);
    else
        err = socketSendTo(sockfd, &ip_addr, sockfd->localPort, buf, len, &written, 0);
    (void) err;
#endif
#ifdef USE_FREERTOS_PLUS
    if (AF_PACKET == family)
        FreeRTOS_sendto(sockfd, buf, len, 0, NULL, 0);
    else
        FreeRTOS_sendto(sockfd, buf, len, 0, &ip_addr, sizeof(ip_addr));
#endif
}  /* send_msg */
#endif

struct task_param {
#ifndef LINUX_PTP
#if( configSUPPORT_STATIC_ALLOCATION == 1 )
    OsPfmTaskHandle_t xTask;
    StaticSemaphore_t xSemaphoreBuffer;
    SemaphoreHandle_t xSemaphore;
#else
    TaskHandle_t xTask;
    SemaphoreHandle_t xSemaphore;
#endif
    int maxfdp;
    void *fd[3];
#endif
    int stop;
};

static void setup_ptp_hw(void)
{
    u8 ptp_domain[8];
    u8 ptp_clk;
    void *fd;
    int p2p;
    int two_step;
    int as;
    struct ptp_clock *c = ptp_clock_ptr[0];
    struct ptp_clock *f[3];
    int i, j;

    fd = dev[1].fd;

    f[0] = f[1] = f[2] = NULL;
    get_clk_domain(fd, &ptp_clk, ptp_domain);
    ptp_domain[0] = 3;
    ptp_domain[1] = 5;
    ptp_domain[2] = 6;
    for (i = 0, j = 0; i < 3; i++) {
        c = ptp_clock_ptr[i];
        if (c && c->ports) {
            ptp_domain[c->index] = c->domain;
            f[j] = c;
            j++;
        }
    }
    set_clk_domain(fd, ptp_clk, ptp_domain);
    if (j > 1) {
        set_2_domain_cfg(fd, 1);
    } else {
        set_2_domain_cfg(fd, 0);
    }

    c = f[0];
    if (c) {
        p2p = (c->delay_mechanism == DELAY_P2P) ? 1 : 0;
        two_step = (c->dds.flags & DDS_TWO_STEP_FLAG) ? 1 : 0;
        as = (c->host_port.gptp) ? 1 : 0;
    } else {
        p2p = 1;
        two_step = 0;
        as = 0;
    }
    set_global_cfg(fd, 0, two_step, p2p, as);

    for (i = 0; i < 3; i++) {
        c = ptp_clock_ptr[i];
        if (c && c->stop)
            clock_start(c);
    }

    ptp_set_notify(fd, 1);
}

static int proc_tx_timestamp(u8 *data, size_t len)
{
    struct ptp_msg_options *param = (struct ptp_msg_options *) data;
    u32 port;
    u32 sec;
    u32 nsec;
    struct ptp_msg msg;

    if (len < sizeof(struct ptp_msg_options)) {
        return DEV_IOC_INVALID_SIZE;
    }
    port = param->port;
    sec = param->ts.t.sec;
    nsec = param->ts.t.nsec;
    memcpy(&msg.hdr.sourcePortIdentity, &param->id,
           sizeof(struct ptp_port_identity));
    msg.hdr.messageType = param->msg;
    msg.hdr.sequenceId = param->seqid;
    msg.hdr.domainNumber = param->domain;
    msg.hdr.messageLength = htons(sizeof(struct ptp_msg_hdr));
    msg.hdr.correctionField = 0;
    msg.hdr.transportSpecific = param->reserved[0];
    mutex_lock(&rx_lock);
    ptp_setup_rx(&msg, port, sec, nsec, true, NULL, 0, 0);
    mutex_unlock(&rx_lock);
    return 0;
}

static void proc_link_status(u8 *data, size_t len)
{
    u8 port = data[0];
    u8 status = data[1];
    int speed = 0;
    struct ptp_port *p;
    int event;
    int state;
    bool half_duplex = false;

    if (status & 1)
        speed = 10;
    else if (status & 2)
        speed = 100;
    else if (status & 4)
        speed = 1000;
    if (status & 0x80)
        half_duplex = true;
#if 0
dbg_msg("p%u=%u:  ", port, speed);
#endif

    p = get_ptp_port(port);
    if (!p)
        return;
    if (speed) {
        if (half_duplex && p->use_delay) {
            p->use_one_way_resp = 1;
        }
#ifdef USE_MULT_RESP
        if (speed == 10 && p->use_delay) {
            p->allow_multi_resp = 1;
            p->peer_valid = 1;
            p->no_id_check = 1;
            p->multi_pdelay = mem_info_alloc(sizeof(struct ptp_multi_pdelay));
            if (p->multi_pdelay)
                init_ptp_multi_pdelay(p);
        }
#endif
        p->report_ready = 1;
        p->no_link = 0;
        event = EVENT_FAULT_CLEAR;

        /* Need to wait for the full timeout before clearing fault? */
        if (p->last_fault)
            event = EVENT_NONE;
    } else {
        p->no_link = 1;
        p->report_ready_done = false;
        event = EVENT_FAULT_SET;
    }
    state = port_event(p, event);
#if 0
dbg_msg("l: %x %d %d %d"NL, p->index, speed, event, state);
#endif
    port_set_state(p, state);
    if (p->c && !p->c->stop) {
        handle_state_decision(p->c);
    }
    if (automotive_test_mode) {
        if (p->report_ready) {
            p->c->report_link = true;
            schedule_delayed_work(&p->c->ethernet_ready_work,
                msecs_to_jiffies(10));
        }
    }
}

void ptp_stack_start_clock(void)
{
    struct ptp_clock *c = ptp_clock_ptr[0];

    /* PTP stack is not running in service mode. */
    if (!c)
        return;
    gptp_get_allowed(&allowed_lost_responses, &allowed_pdelay_faults);
    setup_ptp_hw();
}

void ptp_stack_stop_clock(void)
{
    struct ptp_clock *c = ptp_clock_ptr[0];
    int i;

    for (i = 0; i < 3; i++) {
        c = ptp_clock_ptr[i];
        if (c && c->ports) {
            clock_stop(c);
            reset_ptp_clock(c);
        }
    }
}

#ifdef LINUX_PTP
void *
#else
void
#endif
ptp_notify_task(void *param)
{
    volatile struct task_param *task = param;
    static u8 data[MAX_REQUEST_SIZE];
    int len;

    setup_ptp_hw();

    task->stop = 0;
    do {
#ifdef LINUX_PTP
        len = ptp_recv(dev[1].fd, data, MAX_REQUEST_SIZE);
#else
        len = file_dev_read(dev[1].fd, data, MAX_REQUEST_SIZE, NULL);
#endif
        if (len > 0) {
            if ((data[0] & 0x0f) == PTP_CMD_RESP) {
                switch (data[0] & 0xf0) {
                case PTP_CMD_GET_EVENT:
                    break;
                case PTP_CMD_GET_OUTPUT:
                    break;
                case PTP_CMD_GET_MSG:
                    proc_tx_timestamp(&data[2], len - 2);
                    break;
                case PTP_CMD_GET_LINK:
                    proc_link_status(&data[2], len - 2);
                    break;
                }
            }
        }
    } while (!task->stop);
#ifdef LINUX_PTP
    return NULL;
#else
    task->stop++;
    xSemaphoreGive(task->xSemaphore);
    vTaskDelete(NULL);
#endif
}

#ifndef LINUX_PTP
static sockptr create_sock(char *devname, char *ptp_ip, char *p2p_ip,
    char *local_ip, int port, int multi_loop)
{
#ifdef USE_FREERTOS_PLUS
    struct freertos_sockaddr addr;
#endif
    sockptr sockfd;
    int err;

#ifdef USE_CYCLONETCP
    sockfd = socketOpen(SOCKET_TYPE_DGRAM, SOCKET_IP_PROTO_UDP);
    if (sockfd == NULL)
        return sockfd;

    err = socketBindToInterface(sockfd, &netInterface[0]);
    err = socketBind(sockfd, &IP_ADDR_ANY, port);
    (void) err;
#endif

#ifdef USE_FREERTOS_PLUS
    sockfd = FreeRTOS_socket(FREERTOS_AF_INET, FREERTOS_SOCK_DGRAM,
                 FREERTOS_IPPROTO_UDP);
    if (sockfd == NULL)
        return sockfd;

    addr.sin_port = FreeRTOS_htons(port);
    addr.sin_addr = FreeRTOS_htonl(0xC0A8000A);
    err = FreeRTOS_bind(sockfd, &addr, sizeof(addr));
    if (err) {
        FreeRTOS_closesocket(sockfd);
        return NULL;
    }
#endif

    return sockfd;
}  /* create_sock */

static void enter_multi(void)
{
#ifdef USE_CYCLONETCP
    ipv4JoinMulticastGroup(event_fd->interface, PTP_IPV4_ADDR);
    ipv4JoinMulticastGroup(event_fd->interface, P2P_IPV4_ADDR);
    ipv6JoinMulticastGroup(event_fd->interface, &PTP_IPV6_ADDR);
    ipv6JoinMulticastGroup(event_fd->interface, &P2P_IPV6_ADDR);
#endif
}  /* enter_multi */

static void leave_multi(void)
{
#ifdef USE_CYCLONETCP
    ipv4LeaveMulticastGroup(event_fd->interface, PTP_IPV4_ADDR);
    ipv4LeaveMulticastGroup(event_fd->interface, P2P_IPV4_ADDR);
    ipv6LeaveMulticastGroup(event_fd->interface, &PTP_IPV6_ADDR);
    ipv6LeaveMulticastGroup(event_fd->interface, &P2P_IPV6_ADDR);
#endif
}  /* leave_multi */

#define MAX_MSG_CNT  20

static QueueHandle_t xMsgQueue = NULL;

#if( configSUPPORT_STATIC_ALLOCATION == 1 )
static u8 QueueStorage[MAX_MSG_CNT * sizeof(void *)];
static StaticQueue_t xQueueBuffer;
#endif
#endif

#ifdef LINUX_PTP
static pthread_t task_tid[2];
static struct task_param task_params[2];

static void queue_send(struct ptp_message *m)
{
    struct {
        void *ptr;
    } q_data;
    char *q_ptr = (char *)&q_data;
    mqd_t xMsgQueue;
    int rc;

    xMsgQueue = mq_open("/ptp_msg", O_WRONLY);
    if (xMsgQueue > 0) {
        q_data.ptr = m;
        rc = mq_send(xMsgQueue, q_ptr, sizeof(q_data), 0);
    if (rc)
printf("send err: %d\n", rc);
        mq_close(xMsgQueue);
    }
}
#endif

static
void ptp_save_rx_msg(struct ptp_message *m)
{
    /* message is released in the receive process function. */
    m->save.tx = 0;
    msg_get(m);
#ifdef LINUX_PTP
    queue_send(m);
#else
    xQueueSend(xMsgQueue, &m, 0);
#endif
}

static
void ptp_save_tx_msg(struct ptp_message *m, u8 index, u16 portmap, u8 queue,
    u8 udp, u16 vid, u32 sec, u32 nsec)
{
    /* message will be released after transmit. */
    m->save.tx = 1;
    m->save.udp = udp;
    m->save.index = index;
    m->save.queue = queue;
    m->save.portmap = portmap;
    m->save.vid = vid;
    m->save.sec = sec;
    m->save.nsec = nsec;
#ifdef LINUX_PTP
    queue_send(m);
#else
    xQueueSend(xMsgQueue, &m, 0);
#endif
}

#ifdef LINUX_PTP
void *
#else
void
#endif
ptp_process_task(void *param)
{
#ifdef LINUX_PTP
    volatile struct task_param *task = param;
    struct {
        void *ptr;
    } q_data;
    char *q_ptr = (char *)&q_data;
    struct ptp_message *m;
    struct mq_attr attr;
    mqd_t xMsgQueue;
    ssize_t len;
    int rc;

    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(q_data);
    xMsgQueue = mq_open("/ptp_msg", O_CREAT, S_IRWXU, &attr);
    if (xMsgQueue > 0) {
        rc = mq_getattr(xMsgQueue, &attr);
#if 0
printf("%ld %ld %ld\n", attr.mq_maxmsg, attr.mq_msgsize, attr.mq_curmsgs);
#endif
        mq_close(xMsgQueue);
    } else {
        printf("no msg queue\n");
        return NULL;
    }
#else
    struct ptp_message *m;
    BaseType_t xStatus;

#if( configSUPPORT_STATIC_ALLOCATION == 1 )
    xMsgQueue = xQueueCreateStatic(MAX_MSG_CNT, sizeof(void *),
        QueueStorage, &xQueueBuffer);
#else
    xMsgQueue = xQueueCreate(MAX_MSG_CNT, sizeof(void *));
#endif
#endif
    task->stop = 0;
    do {
#ifdef LINUX_PTP
        xMsgQueue = mq_open("/ptp_msg", O_RDONLY);
        len = mq_receive(xMsgQueue, q_ptr, sizeof(q_data), NULL);
        mq_close(xMsgQueue);
        m = q_data.ptr;
        if (!len || !m)
            break;
#else
        xStatus = xQueueReceive(xMsgQueue, &m, portMAX_DELAY);
        (void) xStatus;
#endif
        if (m->save.tx) {
            struct ptp_msg_save *s = &m->save;

            tx_msg_lock();
            ptp_tx_msg(m, s->index, s->portmap, s->queue, s->udp,
                       s->vid, s->sec, s->nsec, NULL);
            tx_msg_unlock();
        } else {
            mutex_lock(&rx_lock);
            ptp_proc_rx(m);
            mutex_unlock(&rx_lock);
        }
        msg_put(m);
    } while (!task->stop);
#ifdef LINUX_PTP
    rc = mq_unlink("/ptp_msg");
    return NULL;
#else
    vTaskDelete(NULL);
#endif
}

#ifdef LINUX_PTP
static void exit_task(void)
{
    void *status;
    mqd_t xMsgQueue;
    int i, n, rc;

    for (i = 0; i < 2; i++) {
        task_params[i].stop = 1;
    }

    xMsgQueue = mq_open("/ptp_msg", O_WRONLY);
    if (xMsgQueue > 0) {
        rc = mq_send(xMsgQueue, (char *)&i, 0, 0);
    if (rc)
printf("send err: %d\n", rc);
        mq_close(xMsgQueue);
    }

    n = 1;
    if (ptp_hw) {
        rc = ptp_dev_exit(&ptpdev);
        if (rc)
            print_err(rc);
        ptp_api_exit(&ptpdev);
        n = 2;
    }

    for (i = 0; i < n; i++) {
        Pthread_join(task_tid[i], &status);
    }
}

static void init_task(void)
{
    int cap = PTP_KNOW_ABOUT_MULT_PORTS;
    int rc;

    ptpdev.id = 0;
    if (ptp_api_init(&ptpdev))
        return;
    rc = ptp_dev_init(&ptpdev, cap, &ptp_drift, &ptp_version, &ptp_ports,
                      &ptp_host_port);
    if (rc) {
        print_err(rc);
        ptp_api_exit(&ptpdev);
        return;
    }
    ptp_hw = 1;
#if 0
printf("ptp: %d %d %d\n", ptp_version, ptp_ports, ptp_host_port);
#endif
}

static void start_task(void)
{
    Pthread_create(&task_tid[0], NULL, ptp_process_task, &task_params[0]);
    if (ptp_hw)
        Pthread_create(&task_tid[1], NULL, ptp_notify_task, &task_params[1]);
}
#endif

#ifndef LINUX_PTP
struct sock_buf {
    u8 *from;
    u8 *buf;
    size_t len;
};

static int check_dup(struct sock_buf *cur, struct sock_buf *last, int len)
{
    if (cur->len == last->len &&
            memcmp(cur->from, last->from, len) == 0 &&
            memcmp(cur->buf, last->buf, cur->len) == 0)
        return 1;
    return 0;
}

static bool check_raw_socket(sockptr sockfd)
{
#ifdef USE_CYCLONETCP
    return (sockfd->type == SOCKET_TYPE_RAW_ETH);
#endif
#ifdef USE_FREERTOS_PLUS
    int err;
    uint16_t usType;

    err = FreeRTOS_getsockopt(sockfd, 0, FREERTOS_SO_TYPE, &usType,
        sizeof(usType));
    (void) err;
    return usType != 0;
#endif
}

static uint16_t get_vlan(sockptr sockfd)
{
#ifdef USE_FREERTOS_PLUS
    int err;
    uint16_t usVlan = 0;

    err = FreeRTOS_getsockopt(sockfd, 0, FREERTOS_SO_VLAN, &usVlan,
        sizeof(usVlan));
    (void) err;
    return usVlan;
#endif
}

#define MAXBUFFER  1540

void ptp_receive_task(void *param)
{
    struct task_param *task = param;
    struct ptp_msg *msg;
    u8 *recvbuf;
    struct sock_buf buf[1];
    u8 *addr;
    char in_addr[80];
    size_t msglen;
    size_t len;
    int cur;
    sockptr sockfd = eth_fd;
    sockptr fd[3];
#ifdef USE_CYCLONETCP
    SocketEventDesc eventDesc[3];
    IpAddr srcAddr;
    uint16_t srcPort;
#endif
#ifdef USE_FREERTOS_PLUS
    SocketSet_t sockset;
    EventBits_t events;
    struct freertos_sockaddr ip_addr;
    socklen_t addr_len;
    struct tail_tag_info tail_tag;
#endif
    int family;
    int i;
    int maxfdp;
    int err;
    bool raw_packet;
    uint16_t vlan, vid;

    len = (MAXBUFFER + 3) & ~3;
#if( configSUPPORT_STATIC_ALLOCATION == 1 )
    recvbuf = mem_info_alloc(len * 1);
#else
    recvbuf = pvPortMalloc(len * 1);
#endif
    if (!recvbuf) {
        goto rx_task_done;
    }
    buf[0].len = buf[1].len = 0;
    buf[0].buf = recvbuf;
    buf[0].from = &buf[0].buf[6];
    cur = 0;

    maxfdp = task->maxfdp;
    fd[0] = task->fd[0];
    fd[1] = task->fd[1];
    fd[2] = task->fd[2];

    task->stop = 0;
#ifdef USE_FREERTOS_PLUS
    sockset = FreeRTOS_CreateSocketSet();
#endif
    for (i = 0; i < maxfdp; i++) {
#ifdef USE_CYCLONETCP
        eventDesc[i].socket = fd[i];
        eventDesc[i].eventMask = SOCKET_EVENT_RX_READY;
#endif
#ifdef USE_FREERTOS_PLUS
        FreeRTOS_FD_SET(fd[i], sockset, eSELECT_ALL);
#endif
    }
    do {
#ifdef USE_CYCLONETCP
        err = socketPoll(eventDesc, maxfdp, NULL, pdMS_TO_TICKS(100));
        if (err == 101)
            break;
#endif
#ifdef USE_FREERTOS_PLUS
        err = FreeRTOS_select(sockset, pdMS_TO_TICKS(10));

        /* Zero means timeout. */
        if (!err)
            continue;
#endif

        for (i = 0; i < maxfdp; i++) {
            sockfd = fd[i];
#ifdef USE_CYCLONETCP
            if (err ||
                !(eventDesc[i].eventFlags & SOCKET_EVENT_RX_READY))
                continue;
#endif
#ifdef USE_FREERTOS_PLUS
            events = FreeRTOS_FD_ISSET(sockfd, sockset);
            if (!events)
                continue;

            /* Nothing to read. */
            if (!(events & eSELECT_READ))
                continue;
#endif

            /* Clear memory to reveal short PTP messages. */
            memset(buf[cur].buf, 0, sizeof(struct ptp_msg) + 14);
#ifdef USE_CYCLONETCP
            err = socketReceiveFrom(sockfd, &srcAddr, &srcPort,
                                    buf[cur].buf,
                                    MAXBUFFER, &buf[cur].len, 0);
            if (err)
                continue;
#endif
#ifdef USE_FREERTOS_PLUS
            err = FreeRTOS_recvfrom(sockfd, buf[cur].buf,
                                    MAXBUFFER, 0,
                                    &ip_addr, &addr_len);
            if (err <= 0)
                continue;

            buf[cur].len = err;
            err = FreeRTOS_getsockopt(sockfd, 0,
                                      FREERTOS_SO_TAIL_TAG,
                                      &tail_tag, sizeof(tail_tag));
#endif

            raw_packet = check_raw_socket(sockfd);
            vlan = get_vlan(sockfd);
            vid = vlan & 0xfff;
            if (raw_packet) {
                family = AF_PACKET;
                addr = (u8 *) buf[cur].from;
                sprintf(in_addr,
                    "%02x:%02x:%02x:%02x:%02x:%02x [%d]",
                    addr[0], addr[1],
                    addr[2], addr[3],
                    addr[4], addr[5],
                    0);
                len = 6;
            } else {
#ifdef USE_CYCLONETCP
                if (srcAddr.length == sizeof(Ipv4Addr))
                    family = AF_INET;
                else
                    family = AF_INET6;
                ipAddrToString(&srcAddr, in_addr);
                len = srcAddr.length;
#endif
#ifdef USE_FREERTOS_PLUS
                family = AF_INET;
                addr = (u8 *) &ip_addr.sin_addr;
                sprintf(in_addr,
                    "%u.%u.%u.%u",
                    addr[0], addr[1], addr[2], addr[3]);
                len = 4;
#endif
            }
            (void) family;
            msglen = buf[cur].len;
            msg = (struct ptp_msg *) buf[cur].buf;

            if (raw_packet) {
                msg = (struct ptp_msg *) &buf[cur].buf[14];
                msglen -= 14;
            } else {
                addr = NULL;
                msg = (struct ptp_msg *) buf[cur].buf;
            }

#ifdef USE_CYCLONETCP
            if (sockfd->interface) {
                ptp_rx_port = sockfd->interface->rx_port;
                ptp_rx_sec = sockfd->interface->rx_sec;
                ptp_rx_nsec = sockfd->interface->rx_nsec;
            }
#endif
#ifdef USE_FREERTOS_PLUS
            ptp_rx_port = tail_tag.port;
            ptp_rx_sec = tail_tag.sec;
            ptp_rx_nsec = tail_tag.nsec;
#endif
            if (msg->hdr.versionPTP < 2) {
                continue;
            }
            mutex_lock(&rx_lock);
            ptp_setup_rx(msg, ptp_rx_port, ptp_rx_sec, ptp_rx_nsec, false,
                         addr, raw_packet, vid);
            mutex_unlock(&rx_lock);
        }
    } while (!task->stop);
#ifdef USE_FREERTOS_PLUS
    FreeRTOS_DeleteSocketSet(sockset);
#endif

rx_task_done:
    task->stop = 2;
    if (recvbuf)
#if( configSUPPORT_STATIC_ALLOCATION == 1 )
        mem_info_free(recvbuf);
#else
        vPortFree(recvbuf);
#endif
    xSemaphoreGive(task->xSemaphore);
    vTaskDelete(NULL);
}

#ifdef USE_NET_MULTI_IF
struct xNetworkEndPoint;
extern struct xNetworkEndPoint gpxNetworkEndPoint;
#endif

static int get_host_info(struct ip_info *info)
{
    int err = 0;

#ifdef USE_CYCLONETCP
    do {
        MacAddr addr;
        error_t err;

        err = netGetMacAddr(&netInterface[0], &addr);
        if (!err)
            memcpy(info->hwaddr, addr.b, ETH_ALEN);
    } while (0);
#endif
#ifdef USE_FREERTOS_PLUS
    do {
        const uint8_t *addr;
#ifdef USE_NET_MULTI_IF
        addr = FreeRTOS_GetMACAddress(&gpxNetworkEndPoint);
#else
        addr = FreeRTOS_GetMACAddress();
#endif
        memcpy(info->hwaddr, addr, ETH_ALEN);
    } while (0);
#endif

    return err;
}

static void add_multi(sockptr sockfd)
{
#ifdef USE_CYCLONETCP
    NetInterface *interface = sockfd->interface;
    MacAddr addr;
    error_t err;

    if (!interface)
        return;
    memcpy(addr.b, eth_pdelay, sizeof(eth_pdelay));
    err = ethAcceptMacAddr(interface, &addr);
    if (eth_others[0]) {
        memcpy(addr.b, eth_others, sizeof(eth_others));
        err = ethAcceptMacAddr(interface, &addr);
    }
#endif
}

static void del_multi(sockptr sockfd)
{
#ifdef USE_CYCLONETCP
    NetInterface *interface = sockfd->interface;
    MacAddr addr;
    error_t err;

    if (!interface)
        return;
    memcpy(addr.b, eth_pdelay, sizeof(eth_pdelay));
    err = ethDropMacAddr(interface, &addr);
    if (eth_others[0]) {
        memcpy(addr.b, eth_others, sizeof(eth_others));
        err = ethDropMacAddr(interface, &addr);
    }
#endif
}

static sockptr create_raw(struct ip_info *info)
{
    sockptr sockfd;
    struct ethhdr *eh;
    int len = 14 + 2;
    u8 *buf;

#ifdef USE_CYCLONETCP
    sockfd = socketOpen(SOCKET_TYPE_RAW_ETH, ptp_proto);
    if (sockfd == NULL)
        return sockfd;

    socketBindToInterface(sockfd, &netInterface[0]);
#endif

#ifdef USE_FREERTOS_PLUS
    do {
        int err;

        sockfd = FreeRTOS_socket(AF_PACKET, 255, ptp_proto);
        if (sockfd == NULL)
            return sockfd;
        err = FreeRTOS_bind(sockfd, NULL, 0);
        if (err) {
            FreeRTOS_closesocket(sockfd);
            return NULL;
        }
    } while (0);
#endif

#if( configSUPPORT_STATIC_ALLOCATION == 1 )
    buf = mem_info_alloc(len * 3);
#else
    buf = pvPortMalloc(len * 3);
#endif
    eth_pdelay_buf = buf;
    eth_others_buf = buf + len;
    eth_unicast_buf = eth_others_buf + len;
    memcpy(eth_pdelay_buf, eth_pdelay, ETH_ALEN);
    memcpy(&eth_pdelay_buf[ETH_ALEN], info->hwaddr, ETH_ALEN);
    eh = (struct ethhdr *) eth_pdelay_buf;
    eh->proto = htons(ptp_proto);
    memcpy(eth_others_buf, eth_others, ETH_ALEN);
    memcpy(&eth_others_buf[ETH_ALEN], info->hwaddr, ETH_ALEN);
    eh = (struct ethhdr *) eth_others_buf;
    eh->proto = htons(ptp_proto);
    eh = (struct ethhdr *) eth_unicast_buf;
    eh->proto = htons(ptp_proto);

    return sockfd;
}
#endif

static
void ptp_tx_msg(struct ptp_message *m, u8 index, u16 portmap, u8 queue,
    u8 udp, u16 vid, u32 sec, u32 nsec, u8 *dest)
{
    u8 *addr = udp ? NULL : dest;
#ifdef LINUX_PTP
    int rc;
#endif

    ip_family = udp ? AF_INET : AF_PACKET;
    ptp_tx_port = portmap;
    ptp_tx_queue = queue;
    ptp_tx_sec = sec;
    ptp_tx_nsec = nsec;
    memset(&m->rx, 0, sizeof(struct ptp_utime));
#ifdef LINUX_PTP
    rc = set_msg_info(dev[1].fd, &m->msg->hdr, ptp_tx_port, ptp_tx_sec,
        ptp_tx_nsec);
    send_msg(m->msg, ip_family, vid, 0, index, addr);
#else
    send_msg(m, ip_family, vid, 0, index, addr);
#endif
}

#ifndef LINUX_PTP
static struct task_param param[3];

int ptp_stack_main(void)
{
    int rc;
    u8 i, j;
    struct ptp_clock *c;
    u16 ports_left;

    if (ptp_app_init()) {
        return 1;
    }

    ip_family = AF_PACKET;
    memset(&hw_info, 0, sizeof(hw_info));
    get_host_info(&hw_info);

    eth_fd = create_raw(&hw_info);
    if (!eth_fd) {
        ptp_app_cleanup();
        return 1;
    }

    event_fd = create_sock(NULL, ptp_ip, p2p_ip, host_ip,
        PTP_EVENT_PORT, 0);
    general_fd = create_sock(NULL, ptp_ip, p2p_ip, host_ip,
        PTP_GENERAL_PORT, 0);
    if (event_fd)
        enter_multi();
    add_multi(eth_fd);
	
	port_lock_init();
	
    tx_msg_init();

    init_ptp_clocks(hw_info.hwaddr);

    gptp_get_allowed(&allowed_lost_responses, &allowed_pdelay_faults);

    ports_left = (1 << ptp_ports) - 1;

    /* Create first clock. */
    ptp_clock_cnt = 0;
    ptp_clock_domain_ports[0] = ports_left;
    for (i = 0; i < 3; i++) {
        j = i + 1;
        if (gptp_profile_get_domain(j,
                                    &ptp_clock_domains[i],
                                    &ptp_clock_domain_ports[i],
                                    &ptp_clock_vid[i]))
            continue;
        ptp_clock_domain_ports[i] &= ports_left;
        if (!ptp_clock_domain_ports[i])
            continue;

        c =  create_ptp_clock(j, ptp_clock_domains[i],
                              ptp_clock_domain_ports[i],
                              ptp_clock_vid[i]);
        if (c) {
            ports_left &= ~c->ports;
            c->stop = true;
            ptp_clock_ptr[ptp_clock_cnt++] = c;
        }
    }
    if (!ptp_clock_cnt) {
        ptp_app_exit();
        ptp_app_cleanup();
        if (eth_fd) {
            if (event_fd) {
                leave_multi();
                closesocket(event_fd);
            }
            if (general_fd)
                closesocket(general_fd);
            del_multi(eth_fd);
            closesocket(eth_fd);
        }
        return 0;
    }

    memset(param, 0, sizeof(param));

    param[0].xSemaphore = xSemaphoreCreateBinaryStatic(
        &param[0].xSemaphoreBuffer);
    param[1].xSemaphore = xSemaphoreCreateBinaryStatic(
        &param[1].xSemaphoreBuffer);
    param[2].xSemaphore = xSemaphoreCreateBinaryStatic(
        &param[2].xSemaphoreBuffer);

    ptpdev = dev[1].fd;

    param[1].maxfdp = 3;
    param[1].fd[0] = eth_fd;
    param[1].fd[1] = general_fd;
    param[1].fd[2] = event_fd;

    return 0;
}

void ptp_task_run(void *startTask)
{
    if (ptp_stack_main() || !ptp_clock_cnt)
        return;
    ASSERT(os_platform_create_task(PTP_TASK_ID, &param[2]));
}

void ptp_notify_task_run(void *startTask)
{
    if (!ptp_clock_cnt)
        return;
    ASSERT(os_platform_create_task(PTP_NOTIFY_TASK_ID, &param[0]));
}

void ptp_rx_task_run(void *startTask)
{
    if (!ptp_clock_cnt)
        return;
    ASSERT(os_platform_create_task(PTP_RX_TASK_ID, &param[1]));
}
#endif
