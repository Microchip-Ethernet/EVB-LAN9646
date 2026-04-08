/*
    Copyright (C) 2015-2026 Microchip Technology Inc. and its
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

#include <sys/ioctl.h>
#include "os.h"

#ifndef MAX_REQUEST_SIZE
/* Define this for ksz_request. */
#define MAX_REQUEST_SIZE        200
#endif

#include "lan937x_req.h"
#include "lan937x_ptp_api.h"


/* Used to indicate which APIs are supported. */
static u8 _ptp_version;

/* Used to indicate how many ports are in the 1588 PTP device. */
static u8 _ptp_ports;
static u8 _ptp_host_port;

static void exit_req(void *ptr)
{
	struct ksz_request *req = (struct ksz_request *) ptr;

	req->size = SIZEOF_ksz_request;
	req->cmd = DEV_CMD_INFO;
	req->subcmd = DEV_INFO_EXIT;
	req->output = 0;
}  /* exit_req */

static void init_req(void *ptr,
	int capability)
{
	struct ksz_request *req = (struct ksz_request *) ptr;

	req->size = SIZEOF_ksz_request + 8;
	req->cmd = DEV_CMD_INFO;
	req->subcmd = DEV_INFO_INIT;
	req->output = capability;
	req->param.data[0] = '1';
	req->param.data[1] = '5';
	req->param.data[2] = '8';
	req->param.data[3] = '8';
	req->param.data[4] = 'v';
	req->param.data[5] = '2';
}  /* init_req */

static void get_freq_req(void *ptr, int clk_id)
{
	struct ksz_request *req = (struct ksz_request *) ptr;

	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_clk_options);
	req->cmd = DEV_CMD_GET;
	req->subcmd = DEV_PTP_CLK;
	req->output = 1 | (clk_id << 16);
}  /* get_freq_req */

static void adj_freq_req(void *ptr,
	int sign, u32 sec, u32 nsec, int drift, u32 interval, int clk_id)
{
	struct ksz_request *req = (struct ksz_request *) ptr;
	struct ptp_clk_options *param = (struct ptp_clk_options *)
		&req->param;

	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_clk_options);
	req->cmd = DEV_CMD_PUT;
	req->subcmd = DEV_PTP_CLK;
	req->output = sign | (clk_id << 16);
	param->sec = sec;
	param->nsec = nsec;
	param->drift = drift;
	param->interval = interval;
}  /* adj_freq_req */

static void get_clock_req(void *ptr,
	int clk_id)
{
	struct ksz_request *req = (struct ksz_request *) ptr;

	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_clk_options);
	req->cmd = DEV_CMD_GET;
	req->subcmd = DEV_PTP_CLK;
	req->output = clk_id << 16;
}  /* get_clock_req */

static void set_clock_req(void *ptr,
	u32 sec, u32 nsec, int clk_id)
{
	struct ksz_request *req = (struct ksz_request *) ptr;
	struct ptp_clk_options *param = (struct ptp_clk_options *)
		&req->param;

	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_clk_options);
	req->cmd = DEV_CMD_PUT;
	req->subcmd = DEV_PTP_CLK;
	req->output = clk_id << 16;
	param->sec = sec;
	param->nsec = nsec;
}  /* set_clock_req */

static void get_hw_cfg_req(void *ptr)
{
	struct ksz_request *req = (struct ksz_request *) ptr;

	memset(&req->param, 0, sizeof(struct ptp_cfg_options));
	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_cfg_options);
	req->cmd = DEV_CMD_GET;
	req->subcmd = DEV_PTP_CFG;
	req->output = 0;
}  /* get_hw_cfg_req */

static void set_param_master(struct ptp_cfg_options *param, int master)
{
	param->master_set = 1;
	param->master = master ? 1 : 0;
}

static void set_param_2_step(struct ptp_cfg_options *param, int two_step)
{
	param->two_step_set = 1;
	param->two_step = two_step ? 1 : 0;
}

static void set_param_p2p(struct ptp_cfg_options *param, int p2p)
{
	param->p2p_set = 1;
	param->p2p = p2p ? 1 : 0;
}

static void set_param_as(struct ptp_cfg_options *param, int as)
{
	param->as_set = 1;
	param->as = as ? 1 : 0;
}

static void set_param_domain_check(struct ptp_cfg_options *param, int check)
{
	param->domain_check_set = 1;
	param->domain_check = check ? 1 : 0;
}

static void set_param_csum(struct ptp_cfg_options *param, int csum)
{
	param->udp_csum_set = 1;
	param->udp_csum = csum ? 1 : 0;
}

static void set_param_unicast(struct ptp_cfg_options *param, int unicast)
{
	param->unicast_set = 1;
	param->unicast = unicast ? 1 : 0;
}

static void set_param_alternate(struct ptp_cfg_options *param, int alternate)
{
	param->alternate_set = 1;
	param->alternate = alternate ? 1 : 0;
}

static void set_param_delay_assoc(struct ptp_cfg_options *param, int assoc)
{
	param->delay_assoc_set = 1;
	param->delay_assoc = assoc ? 1 : 0;
}

static void set_param_pdelay_assoc(struct ptp_cfg_options *param, int assoc)
{
	param->pdelay_assoc_set = 1;
	param->pdelay_assoc = assoc ? 1 : 0;
}

static void set_param_sync_assoc(struct ptp_cfg_options *param, int assoc)
{
	param->sync_assoc_set = 1;
	param->sync_assoc = assoc ? 1 : 0;
}

static void set_param_drop_sync(struct ptp_cfg_options *param, int drop)
{
	param->drop_sync_set = 1;
	param->drop_sync = drop ? 1 : 0;
}

static void set_param_priority(struct ptp_cfg_options *param, int priority)
{
	param->priority_set = 1;
	param->priority = priority ? 1 : 0;
}

static void set_param_domain(struct ptp_cfg_options *param, int domain)
{
	param->domain_set = 1;
	param->domain = (u8) domain;
}

static void set_hw_cfg_req(void *ptr,
	void (*set_param)(struct ptp_cfg_options *param, int set), int set)
{
	struct ksz_request *req = (struct ksz_request *) ptr;
	struct ptp_cfg_options *param = (struct ptp_cfg_options *)
		&req->param;

	memset(&req->param, 0, sizeof(struct ptp_cfg_options));
	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_cfg_options);
	req->cmd = DEV_CMD_PUT;
	req->subcmd = DEV_PTP_CFG;
	req->output = 0;
	set_param(param, set);
}  /* set_hw_cfg_req */

static void set_cfg_req(void *ptr,
	int master, int two_step, int p2p, int as)
{
	struct ksz_request *req = (struct ksz_request *) ptr;
	struct ptp_cfg_options *param = (struct ptp_cfg_options *)
		&req->param;

	memset(&req->param, 0, sizeof(struct ptp_cfg_options));
	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_cfg_options);
	req->cmd = DEV_CMD_PUT;
	req->subcmd = DEV_PTP_CFG;
	req->output = 0;
	set_param_master(param, master);
	set_param_2_step(param, two_step);
	set_param_p2p(param, p2p);
	set_param_as(param, as);
}  /* set_cfg_req */

static void get_2_domain_cfg_req(void *ptr,
	int port)
{
	struct ksz_request *req = (struct ksz_request *) ptr;

	memset(&req->param, 0, sizeof(struct ptp_cfg_options));
	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_cfg_options);
	req->cmd = DEV_CMD_GET;
	req->subcmd = DEV_PTP_CFG;
	req->output = port;
}  /* get_2_domain_cfg_req */

static void set_2_domain_cfg_req(void *ptr,
	int domain)
{
	struct ksz_request *req = (struct ksz_request *) ptr;
	struct ptp_cfg_options *param = (struct ptp_cfg_options *)
		&req->param;

	memset(&req->param, 0, sizeof(struct ptp_cfg_options));
	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_cfg_options);
	req->cmd = DEV_CMD_PUT;
	req->subcmd = DEV_PTP_CFG;
	req->output = -1;
	set_param_domain_check(param, domain);
}  /* set_2_domain_cfg_req */

static void set_2_domain_port_cfg_req(void *ptr,
	int port, int two_step, int p2p)
{
	struct ksz_request *req = (struct ksz_request *) ptr;
	struct ptp_cfg_options *param = (struct ptp_cfg_options *)
		&req->param;

	memset(&req->param, 0, sizeof(struct ptp_cfg_options));
	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_cfg_options);
	req->cmd = DEV_CMD_PUT;
	req->subcmd = DEV_PTP_CFG;
	req->output = port;
	set_param_2_step(param, two_step);
	set_param_p2p(param, p2p);
}  /* set_2_domain_port_cfg_req */

static void get_delays_req(void *ptr,
	int port)
{
	struct ksz_request *req = (struct ksz_request *) ptr;

	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_delay_values);
	req->cmd = DEV_CMD_GET;
	req->subcmd = DEV_PTP_DELAY;
	req->output = port;
}  /* get_delays_req */

static void set_delays_req(void *ptr,
	int port, int rx, int tx, int asym)
{
	struct ksz_request *req = (struct ksz_request *) ptr;
	struct ptp_delay_values *param = (struct ptp_delay_values *)
		&req->param;

	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_delay_values);
	req->cmd = DEV_CMD_PUT;
	req->subcmd = DEV_PTP_DELAY;
	req->output = port;
	param->rx_latency = rx;
	param->tx_latency = tx;
	param->asym_delay = asym;
	param->reserved = 0;
}  /* set_delays_req */

static void get_reg_req(void *ptr,
	size_t width, unsigned int reg)
{
	struct ksz_request *req = (struct ksz_request *) ptr;

	req->size = SIZEOF_ksz_request;
	req->cmd = DEV_CMD_GET;
	req->subcmd = DEV_PTP_REG;
	if (_ptp_version >= 2) {
		req->size += sizeof(u32) * 3;
		req->output = 0;
		req->param.num[0] = width;
		req->param.num[1] = reg;
	} else
		req->output = reg;
}  /* get_reg_req */

static void set_reg_req(void *ptr,
	size_t width, unsigned int reg, unsigned int val)
{
	struct ksz_request *req = (struct ksz_request *) ptr;

	req->size = SIZEOF_ksz_request;
	req->cmd = DEV_CMD_PUT;
	req->subcmd = DEV_PTP_REG;
	if (_ptp_version >= 2) {
		req->size += sizeof(u32) * 3;
		req->output = 0;
		req->param.num[0] = width;
		req->param.num[1] = reg;
		req->param.num[2] = val;
	} else
		req->output = (reg & 0xffff) | (val << 16);
}  /* set_reg_req */

static void get_clock_ident_req(void *ptr,
	int master)
{
	struct ksz_request *req = (struct ksz_request *) ptr;

	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_clock_identity);
	req->cmd = DEV_CMD_GET;
	req->subcmd = DEV_PTP_IDENTITY;
	if (_ptp_version >= 2)
		req->output = master;
	else
		req->output = 0;
}  /* get_clock_ident_req */

static void set_clock_ident_req(void *ptr,
	int master, struct ptp_clock_identity *id)
{
	struct ksz_request *req = (struct ksz_request *) ptr;

	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_clock_identity);
	req->cmd = DEV_CMD_PUT;
	req->subcmd = DEV_PTP_IDENTITY;
	if (_ptp_version >= 2)
		req->output = master;
	else
		req->output = 0;
	memcpy(&req->param, id, sizeof(struct ptp_clock_identity));
}  /* set_clock_ident_req */

static void get_peer_delay_req(void *ptr,
	int port)
{
	struct ksz_request *req = (struct ksz_request *) ptr;

	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_delay_values);
	req->cmd = DEV_CMD_GET;
	req->subcmd = DEV_PTP_PEER_DELAY;
	req->output = port;
}  /* get_peer_delay_req */

static void set_peer_delay_req(void *ptr,
	int port, int delay)
{
	struct ksz_request *req = (struct ksz_request *) ptr;
	struct ptp_delay_values *param = (struct ptp_delay_values *)
		&req->param;

	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_delay_values);
	req->cmd = DEV_CMD_PUT;
	req->subcmd = DEV_PTP_PEER_DELAY;
	req->output = port;
	param->rx_latency = 0;
	param->tx_latency = 0;
	param->asym_delay = 0;
	param->reserved = delay;
	if (_ptp_version >= 2)
		param->rx_latency = delay >> 16;
}  /* set_peer_delay_req */

static void get_port_cfg_req(void *ptr,
	int port)
{
	struct ksz_request *req = (struct ksz_request *) ptr;

	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_delay_values);
	req->cmd = DEV_CMD_GET;
	req->subcmd = DEV_PTP_PORT_CFG;
	req->output = port;
}  /* get_port_cfg_req */

static void set_port_cfg_req(void *ptr,
	int port, int enable, int asCapable)
{
	struct ksz_request *req = (struct ksz_request *) ptr;
	struct ptp_delay_values *param = (struct ptp_delay_values *)
		&req->param;

	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_delay_values);
	req->cmd = DEV_CMD_PUT;
	req->subcmd = DEV_PTP_PORT_CFG;
	req->output = port;
	param->reserved = 0;
	if (enable)
		param->reserved |= PTP_PORT_ENABLED;
	if (asCapable)
		param->reserved |= PTP_PORT_ASCAPABLE;
}  /* set_port_cfg_req */

static void get_utc_offset_req(void *ptr)
{
	struct ksz_request *req = (struct ksz_request *) ptr;

	req->size = SIZEOF_ksz_request;
	req->cmd = DEV_CMD_GET;
	req->subcmd = DEV_PTP_UTC_OFFSET;
	req->output = 0;
}  /* get_utc_offset_req */

static void set_utc_offset_req(void *ptr,
	int offset)
{
	struct ksz_request *req = (struct ksz_request *) ptr;

	req->size = SIZEOF_ksz_request;
	req->cmd = DEV_CMD_PUT;
	req->subcmd = DEV_PTP_UTC_OFFSET;
	req->output = offset;
}  /* set_utc_offset_req */

static void get_clk_domain_req(void *ptr)
{
	struct ksz_request *req = (struct ksz_request *) ptr;

	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_clk_domains);
	req->cmd = DEV_CMD_GET;
	req->subcmd = DEV_PTP_CLK_DOMAIN;
	req->output = 0;
}  /* get_clk_domain_req */

static void set_clk_domain_req(void *ptr,
	int clk, u8 domains[])
{
	struct ksz_request *req = (struct ksz_request *) ptr;
	struct ptp_clk_domains *param = (struct ptp_clk_domains *)
		&req->param;
	int i;

	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_clk_domains);
	req->cmd = DEV_CMD_PUT;
	req->subcmd = DEV_PTP_CLK_DOMAIN;
	req->output = 0;
	param->clk = clk;
	for (i = 0; i < clk; i++)
		param->domains[i] = domains[i];
}  /* set_clk_domain_req */

static void get_clk_domain_ports_req(void *ptr)
{
	struct ksz_request *req = (struct ksz_request *) ptr;

	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_clk_domains);
	req->cmd = DEV_CMD_GET;
	req->subcmd = DEV_PTP_CLK_DOMAIN;
	req->output = 1;
}  /* get_clk_domain_ports_req */

static void set_clk_domain_ports_req(void *ptr,
	int clk, u8 ports[])
{
	struct ksz_request *req = (struct ksz_request *) ptr;
	struct ptp_clk_domains *param = (struct ptp_clk_domains *)
		&req->param;
	int i;

	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_clk_domains);
	req->cmd = DEV_CMD_PUT;
	req->subcmd = DEV_PTP_CLK_DOMAIN;
	req->output = 1;
	param->clk = clk;
	for (i = 0; i < clk; i++)
		param->domains[i] = ports[i];
}  /* set_clk_domain_ports_req */

static void get_timestamp_req(void *ptr,
	u8 msg, u8 port, u16 seqid, u8 mac[], u32 timestamp)
{
	struct ksz_request *req = (struct ksz_request *) ptr;
	struct ptp_ts_options *param = (struct ptp_ts_options *)
		&req->param;

	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_ts_options);
	req->cmd = DEV_CMD_GET;
	req->subcmd = DEV_PTP_TIMESTAMP;
	req->output = 0;
	param->timestamp = timestamp;
	param->msg = msg;
	param->port = port;
	param->seqid = seqid;
	param->mac[0] = mac[0];
	param->mac[1] = mac[1];
}  /* get_timestamp_req */

static void get_msg_info_req(void *ptr,
	void *buf, int tx)
{
	struct ksz_request *req = (struct ksz_request *) ptr;
	struct ptp_msg_options *param = (struct ptp_msg_options *)
		&req->param;
	struct ptp_msg_hdr *hdr = buf;

	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_msg_options);
	req->cmd = DEV_CMD_GET;
	req->subcmd = DEV_PTP_MSG;
	req->output = tx;
	memcpy(&param->id, &hdr->sourcePortIdentity,
		sizeof(struct ptp_port_identity));
	param->msg = hdr->messageType;
	param->seqid = hdr->sequenceId;
	param->domain = hdr->domainNumber;
}  /* get_msg_info_req */

static void set_msg_info_req(void *ptr,
	void *buf, u32 port, u32 sec, u32 nsec)
{
	struct ksz_request *req = (struct ksz_request *) ptr;
	struct ptp_msg_options *param = (struct ptp_msg_options *)
		&req->param;
	struct ptp_msg_hdr *hdr = buf;

	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_msg_options);
	req->cmd = DEV_CMD_PUT;
	req->subcmd = DEV_PTP_MSG;
	req->output = 0;
	memcpy(&param->id, &hdr->sourcePortIdentity,
		sizeof(struct ptp_port_identity));
	param->msg = hdr->messageType;
	param->seqid = hdr->sequenceId;
	param->domain = hdr->domainNumber;
	param->port = port;
	param->ts.timestamp = (sec << 30) | nsec;
	param->ts.t.sec = sec;
	param->ts.t.nsec = nsec;
}  /* set_msg_info_req */

static void get_tail_tag_req(void *ptr,
	void *buf, int len, u8 *tag, int tag_len)
{
	struct ksz_request *req = (struct ksz_request *) ptr;
	struct ptp_msg_tag *param = (struct ptp_msg_tag *)
		&req->param;

	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_msg_tag);
	req->cmd = DEV_CMD_PUT;
	req->subcmd = DEV_PTP_TAG;
	param->len = (u16) len;
	param->tag_len = (u16) tag_len;
	param->dst = NULL;
	param->msg = buf;
	param->tag = tag;
}  /* get_tail_tag_req */

static void set_tail_tag_req(void *ptr,
	void *dst, void *buf, int len, uint ports, u8 queue, int tag_len)
{
	struct ksz_request *req = (struct ksz_request *) ptr;
	struct ptp_msg_tag *param = (struct ptp_msg_tag *)
		&req->param;

	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_msg_tag);
	req->cmd = DEV_CMD_GET;
	req->subcmd = DEV_PTP_TAG;
	param->len = (u16) len;
	param->tag_len = (u16) tag_len;
	param->dst = dst;
	param->msg = buf;
	param->ports = ports;
	param->queue = queue;
}  /* set_tail_tag_req */

static void rx_event_req(void *ptr,
	u8 tsi, u8 gpi, u8 event, u8 total, u8 flags, u32 timeout)
{
	struct ksz_request *req = (struct ksz_request *) ptr;
	struct ptp_tsi_options *param = (struct ptp_tsi_options *)
		&req->param;

	memset(&req->param, 0, sizeof(struct ptp_tsi_options));
	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_tsi_options);
	req->cmd = DEV_CMD_PUT;
	req->subcmd = DEV_PTP_TEVT;
	param->tsi = tsi;
	param->gpi = gpi;
	param->event = event;
	param->flags = flags;
	param->total = total;
	param->timeout = timeout;
}  /* rx_event_req */

static void rx_get_clock_req(void *ptr,
	u8 tsi)
{
	struct ksz_request *req = (struct ksz_request *) ptr;
	struct ptp_tsi_options *param = (struct ptp_tsi_options *)
		&req->param;

	memset(&req->param, 0, sizeof(struct ptp_tsi_options));
	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_tsi_options);
	req->cmd = DEV_CMD_GET;
	req->subcmd = DEV_PTP_TEVT;
	req->output = 8;
	param->tsi = tsi;
}  /* rx_get_clock_req */

static void rx_set_clock_req(void *ptr,
	u8 tsi, int clk_id)
{
	struct ksz_request *req = (struct ksz_request *) ptr;
	struct ptp_tsi_options *param = (struct ptp_tsi_options *)
		&req->param;

	memset(&req->param, 0, sizeof(struct ptp_tsi_options));
	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_tsi_options);
	req->cmd = DEV_CMD_PUT;
	req->subcmd = DEV_PTP_TEVT;
	req->output = 8 | (clk_id << 16);
	param->tsi = tsi;
}  /* rx_set_clock_req */

static void tx_event_req(void *ptr,
	u8 tso, u8 gpo, u8 event, u32 pulse, u32 cycle, u16 cnt,
	u32 iterate, u32 sec, u32 nsec, u8 flags)
{
	struct ksz_request *req = (struct ksz_request *) ptr;
	struct ptp_tso_options *param = (struct ptp_tso_options *)
		&req->param;

	memset(&req->param, 0, sizeof(struct ptp_tso_options));
	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_tso_options);
	req->cmd = DEV_CMD_PUT;
	req->subcmd = DEV_PTP_TOUT;
	param->tso = tso;
	param->gpo = gpo;
	param->event = event;
	param->flags = flags;
	param->pulse = pulse;
	param->cycle = cycle;
	param->cnt = cnt;
	param->sec = sec;
	param->nsec = nsec;
	param->iterate = iterate;
}  /* tx_event_req */

static void tx_cascade_req(void *ptr,
	u8 tso, u8 gpo, u8 total, u16 cnt, u8 flags)
{
	struct ksz_request *req = (struct ksz_request *) ptr;
	struct ptp_tso_options *param = (struct ptp_tso_options *)
		&req->param;

	memset(&req->param, 0, sizeof(struct ptp_tso_options));
	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_tso_options);
	req->cmd = DEV_CMD_PUT;
	req->subcmd = DEV_PTP_CASCADE;
	req->output = 1;
	param->tso = tso;
	param->gpo = gpo;
	param->total = total;
	param->flags = flags;
	param->cnt = cnt;
}  /* tx_cascade_req */

static void tx_cascade_init_req(void *ptr,
	u8 tso, u8 gpo, u8 total, u8 flags)
{
	struct ksz_request *req = (struct ksz_request *) ptr;
	struct ptp_tso_options *param = (struct ptp_tso_options *)
		&req->param;

	memset(&req->param, 0, sizeof(struct ptp_tso_options));
	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_tso_options);
	req->cmd = DEV_CMD_PUT;
	req->subcmd = DEV_PTP_CASCADE;
	req->output = 0;
	param->tso = tso;
	param->gpo = gpo;
	param->total = total;
	param->flags = flags;
}  /* tx_cascade_init_req */

static void tx_get_output_req(void *ptr,
	u8 gpo)
{
	struct ksz_request *req = (struct ksz_request *) ptr;
	struct ptp_tso_options *param = (struct ptp_tso_options *)
		&req->param;

	memset(&req->param, 0, sizeof(struct ptp_tso_options));
	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_tso_options);
	req->cmd = DEV_CMD_GET;
	req->subcmd = DEV_PTP_TOUT;
	req->output = 0;
	param->gpo = gpo;
}  /* tx_get_output_req */

static void tx_get_clock_req(void *ptr,
	u8 tso)
{
	struct ksz_request *req = (struct ksz_request *) ptr;
	struct ptp_tso_options *param = (struct ptp_tso_options *)
		&req->param;

	memset(&req->param, 0, sizeof(struct ptp_tso_options));
	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_tso_options);
	req->cmd = DEV_CMD_GET;
	req->subcmd = DEV_PTP_TOUT;
	req->output = 8;
	param->tso = tso;
}  /* tx_get_clock_req */

static void tx_set_clock_req(void *ptr,
	u8 tso, int clk_id)
{
	struct ksz_request *req = (struct ksz_request *) ptr;
	struct ptp_tso_options *param = (struct ptp_tso_options *)
		&req->param;

	memset(&req->param, 0, sizeof(struct ptp_tso_options));
	req->size = SIZEOF_ksz_request;
	req->size += sizeof(struct ptp_tso_options);
	req->cmd = DEV_CMD_PUT;
	req->subcmd = DEV_PTP_TOUT;
	req->output = 8 | (clk_id << 16);
	param->tso = tso;
}  /* tx_set_clock_req */

static void get_rx_event_req(void *ptr,
	u8 tsi)
{
	struct ksz_request *req = (struct ksz_request *) ptr;
	struct ptp_tsi_info *param = (struct ptp_tsi_info *)
		&req->param;

	req->size = SIZEOF_ksz_request;
	req->size += SIZEOF_ptp_tsi_info;
	req->cmd = DEV_CMD_GET;
	req->subcmd = DEV_PTP_TEVT;
	req->output = 0;
	param->cmd = PTP_CMD_GET_EVENT;
	param->unit = tsi;
}  /* get_rx_event_req */

static void poll_rx_event_req(void *ptr,
	u8 tsi)
{
	struct ksz_request *req = (struct ksz_request *) ptr;
	struct ptp_tsi_info *param = (struct ptp_tsi_info *)
		&req->param;

	req->size = SIZEOF_ksz_request;
	req->size += SIZEOF_ptp_tsi_info;
	req->cmd = DEV_CMD_GET;
	req->subcmd = DEV_PTP_TEVT;
	req->output = 1;
	param->cmd = PTP_CMD_GET_EVENT;
	param->unit = tsi;
}  /* poll_rx_event_req */

static void get_rx_event_info_req(void *ptr)
{
	struct ksz_request *req = (struct ksz_request *) ptr;
	struct ptp_tsi_info *param = (struct ptp_tsi_info *)
		&req->param;

	req->size = SIZEOF_ksz_request;
	req->size += SIZEOF_ptp_tsi_info;
	req->cmd = DEV_CMD_GET;
	req->subcmd = DEV_PTP_TEVT;
	req->output = 2;
	param->cmd = PTP_CMD_GET_EVENT;

	/*
	 * Old implementation will think this is poll event call and reject it
	 * because of invalid unit.
	 */
	param->unit = -1;
}  /* get_rx_event_info_req */

int ptp_dev_exit(void *fd)
{
	struct ksz_request_actual req;
	int rc;

	exit_req(&req);
	rc = file_dev_ioctl(fd, &req);
	return rc;
}

int ptp_dev_init(void *fd,
	int capability, int *drift, u8 *version, u8 *ports, u8 *host_port)
{
	struct ksz_request_actual req;
	int rc;

	init_req(&req, capability);
	rc = file_dev_ioctl(fd, &req);
	if (!rc) {
		if ('M' == req.param.data[0] &&
				'i' == req.param.data[1] &&
				'c' == req.param.data[2] &&
				'r' == req.param.data[3]) {
			*version = req.param.data[4];
			*ports = req.param.data[5];
			if (*version >= 2) {
				*host_port = req.param.data[6];
				if (*host_port)
					*ports -= 1;
			}
			_ptp_version = *version;
			_ptp_ports = *ports;
			_ptp_host_port = *host_port;
		}
		*drift = req.output;
	}
	return rc;
}

#define SW_INFO_LINK_CHANGE		(1 << 0)

int ptp_set_notify(void *fd,
	uint notify)
{
	struct ksz_request_actual req;
	int rc;

	req.size = SIZEOF_ksz_request;
	req.size += sizeof(uint);
	req.cmd = DEV_CMD_INFO;
	req.subcmd = DEV_INFO_NOTIFY;
	req.output = 0;
	memcpy(&req.param, &notify, sizeof(uint));
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	return rc;
}  /* ptp_set_notify */

int ptp_port_info(void *fd,
	char *name, u8 *port, u32 *mask)
{
	struct ksz_request_actual req;
	int len;
	int rc;

	len = strlen(name);
	req.size = SIZEOF_ksz_request + len + 2;
	req.cmd = DEV_CMD_INFO;
	req.subcmd = DEV_INFO_PORT;
	memcpy(&req.param.data, name, len);
	req.param.data[len] = '\0';
	rc = file_dev_ioctl(fd, &req);
	if (!rc) {
		if ('M' == req.param.data[0] &&
				'i' == req.param.data[1] &&
				'c' == req.param.data[2] &&
				'r' == req.param.data[3]) {
			*port = req.param.data[4];
			*mask = req.output;
		}
	}
	return rc;
}

int get_freq(void *fd,
	int *drift, int clk_id)
{
	struct ksz_request_actual req;
	int rc;

	if (_ptp_version < 2)
		return DEV_IOC_INVALID_CMD;
	get_freq_req(&req, clk_id);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	if (!rc)
		*drift = req.output;
	return rc;
}  /* get_freq */

int adj_freq(void *fd,
	int sec, int nsec, int drift, u32 interval, int clk_id)
{
	struct ksz_request_actual req;
	int rc;
	int sign;

	if (nsec < 0 || sec < 0) {
		sign = 1;
		sec = -sec;
		nsec = -nsec;
	} else
		sign = 2;
	adj_freq_req(&req, sign, sec, nsec, drift, interval, clk_id);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	return rc;
}  /* adj_freq */

int get_clock(void *fd,
	uint *sec, uint *nsec, int clk_id)
{
	struct ksz_request_actual req;
	int rc;

	get_clock_req(&req, clk_id);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	if (!rc) {
		struct ptp_clk_options *param = (struct ptp_clk_options *)
			&req.param;

		*sec = param->sec;
		*nsec = param->nsec;
	}
	return rc;
}  /* get_clock */

int set_clock(void *fd,
	u32 sec, u32 nsec, int clk_id)
{
	struct ksz_request_actual req;
	int rc;

	set_clock_req(&req, sec, nsec, clk_id);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	return rc;
}  /* set_clock */

int get_global_cfg(void *fd,
	int *master, int *two_step, int *p2p, int *as,
	int *unicast, int *alternate, int *csum, int *check,
	int *delay_assoc, int *pdelay_assoc, int *sync_assoc, int *drop_sync,
	int *priority, u8 *domain, u32 *delay, int *started)
{
	struct ksz_request_actual req;
	struct ptp_cfg_options *param = (struct ptp_cfg_options *) &req.param;
	int rc;

	get_hw_cfg_req(&req);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	if (!rc) {
		if (master)
			*master = param->master;
		if (two_step)
			*two_step = param->two_step;
		if (p2p)
			*p2p = param->p2p;
		if (as)
			*as = param->as;
		if (unicast)
			*unicast = param->unicast;
		if (alternate)
			*alternate = param->alternate;
		if (csum)
			*csum = param->udp_csum;
		if (check)
			*check = param->domain_check;
		if (delay_assoc)
			*delay_assoc = param->delay_assoc;
		if (pdelay_assoc)
			*pdelay_assoc = param->pdelay_assoc;
		if (sync_assoc)
			*sync_assoc = param->sync_assoc;
		if (drop_sync)
			*drop_sync = param->drop_sync;
		if (priority)
			*priority = param->priority;
		if (domain)
			*domain = param->domain;
		if (delay)
			*delay = param->access_delay;
		if (started)
			*started = param->reserved;
	}
	return rc;
}  /* get_global_cfg */

static int set_hw_cfg(void *fd,
	void (*set_param)(struct ptp_cfg_options *param, int set), int set)
{
	struct ksz_request_actual req;
	int rc;	

	set_hw_cfg_req(&req, set_param, set);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	return rc;
}  /* set_hw_cfg */

int set_hw_domain(void *fd,
	u8 domain)
{
	return set_hw_cfg(fd, set_param_domain, domain);
}

int set_hw_master(void *fd,
	int master)
{
	return set_hw_cfg(fd, set_param_master, master);
}

int set_hw_2_step(void *fd,
	int two_step)
{
	return set_hw_cfg(fd, set_param_2_step, two_step);
}

int set_hw_p2p(void *fd,
	int p2p)
{
	return set_hw_cfg(fd, set_param_p2p, p2p);
}

int set_hw_as(void *fd,
	int as)
{
	return set_hw_cfg(fd, set_param_as, as);
}

int set_hw_unicast(void *fd,
	int unicast)
{
	return set_hw_cfg(fd, set_param_unicast, unicast);
}

int set_hw_alternate(void *fd,
	int alternate)
{
	return set_hw_cfg(fd, set_param_alternate, alternate);
}

int set_hw_domain_check(void *fd,
	int check)
{
	return set_hw_cfg(fd, set_param_domain_check, check);
}

int set_hw_csum(void *fd,
	int csum)
{
	return set_hw_cfg(fd, set_param_csum, csum);
}

int set_hw_delay_assoc(void *fd,
	int assoc)
{
	return set_hw_cfg(fd, set_param_delay_assoc, assoc);
}

int set_hw_pdelay_assoc(void *fd,
	int assoc)
{
	return set_hw_cfg(fd, set_param_pdelay_assoc, assoc);
}

int set_hw_sync_assoc(void *fd,
	int assoc)
{
	return set_hw_cfg(fd, set_param_sync_assoc, assoc);
}

int set_hw_drop_sync(void *fd,
	int drop)
{
	return set_hw_cfg(fd, set_param_drop_sync, drop);
}

int set_hw_priority(void *fd,
	int priority)
{
	return set_hw_cfg(fd, set_param_priority, priority);
}

int set_global_cfg(void *fd,
	int master, int two_step, int p2p, int as)
{
	struct ksz_request_actual req;
	int rc;

	set_cfg_req(&req, master, two_step, p2p, as);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	return rc;
}  /* set_global_cfg */

int get_2_domain_cfg(void *fd,
	int *domain)
{
	struct ksz_request_actual req;
	struct ptp_cfg_options *param = (struct ptp_cfg_options *) &req.param;
	int rc;

	get_2_domain_cfg_req(&req, -1);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	if (!rc) {
		if (domain)
			*domain = param->domain_check;
	}
	return rc;
}  /* get_2_domain_cfg */

int set_2_domain_cfg(void *fd,
	int domain)
{
	struct ksz_request_actual req;
	int rc;

	set_2_domain_cfg_req(&req, domain);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	return rc;
}  /* set_2_domain_cfg */

int get_2_domain_port_cfg(void *fd,
	int port, int *two_step, int *p2p)
{
	struct ksz_request_actual req;
	struct ptp_cfg_options *param = (struct ptp_cfg_options *) &req.param;
	int rc;

	get_2_domain_cfg_req(&req, port + 1);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	if (!rc) {
		if (two_step)
			*two_step = param->two_step;
		if (p2p)
			*p2p = param->p2p;
	}
	return rc;
}  /* get_2_domain_port_cfg */

int set_2_domain_port_cfg(void *fd,
	int port, int two_step, int p2p)
{
	struct ksz_request_actual req;
	int rc;

	set_2_domain_port_cfg_req(&req, port + 1, two_step, p2p);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	return rc;
}  /* set_2_domain_port_cfg */

int get_delays(void *fd,
	int port, int *rx, int *tx, int *asym)
{
	struct ksz_request_actual req;
	int rc;

	get_delays_req(&req, port);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	if (!rc) {
		struct ptp_delay_values *param = (struct ptp_delay_values *)
			&req.param;

		*rx = param->rx_latency;
		*tx = param->tx_latency;
		*asym = param->asym_delay;
	}
	return rc;
}  /* get_delays */

int set_delays(void *fd,
	int port, int rx, int tx, int asym)
{
	struct ksz_request_actual req;
	int rc;

	set_delays_req(&req, port, rx, tx, asym);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	return rc;
}  /* set_delays */

int get_reg(void *fd,
	size_t width, uint reg, uint *val)
{
	struct ksz_request_actual req;
	int rc;

	if (_ptp_version < 2 && width != 2)
		return DEV_IOC_INVALID_CMD;
	get_reg_req(&req, width, reg);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	if (!rc)
		*val = req.output;
	return rc;
}  /* get_reg */

int set_reg(void *fd,
	size_t width, uint reg, uint val)
{
	struct ksz_request_actual req;
	int rc;

	if (_ptp_version < 2 && width != 2)
		return DEV_IOC_INVALID_CMD;
	set_reg_req(&req, width, reg, val);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	return rc;
}  /* set_reg */

int get_clock_ident(void *fd,
	int master, struct ptp_clock_identity *id)
{
	struct ksz_request_actual req;
	int rc;

	if (_ptp_version < 2 && master)
		return DEV_IOC_INVALID_CMD;
	get_clock_ident_req(&req, master);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	if (!rc)
		memcpy(id, &req.param, sizeof(struct ptp_clock_identity));
	return rc;
}  /* get_clock_ident */

int set_clock_ident(void *fd,
	int master, struct ptp_clock_identity *id)
{
	struct ksz_request_actual req;
	int rc;

	if (_ptp_version < 2 && master)
		return DEV_IOC_INVALID_CMD;
	set_clock_ident_req(&req, master, id);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	return rc;
}  /* set_clock_ident */

int get_peer_delay(void *fd,
	int port, int *delay)
{
	struct ksz_request_actual req;
	int rc;

	get_peer_delay_req(&req, port);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	if (!rc) {
		struct ptp_delay_values *param = (struct ptp_delay_values *)
			&req.param;

		*delay = param->rx_latency;
		*delay <<= 16;
		*delay |= param->reserved;
	}
	return rc;
}  /* get_peer_delay */

int set_peer_delay(void *fd,
	int port, int delay)
{
	struct ksz_request_actual req;
	int rc;

	if (_ptp_version < 2 && delay >= 0x10000)
		return DEV_IOC_INVALID_CMD;
	set_peer_delay_req(&req, port, delay);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	return rc;
}  /* set_peer_delay */

int get_port_cfg(void *fd,
	int port, int *enable, int *asCapable)
{
	struct ksz_request_actual req;
	int rc;

	get_port_cfg_req(&req, port);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	if (!rc) {
		struct ptp_delay_values *param = (struct ptp_delay_values *)
			&req.param;

		*enable = !!(param->reserved & PTP_PORT_ENABLED);
		*asCapable = !!(param->reserved & PTP_PORT_ASCAPABLE);
	}
	return rc;
}  /* get_port_cfg */

int set_port_cfg(void *fd,
	int port, int enable, int asCapable)
{
	struct ksz_request_actual req;
	int rc;

	set_port_cfg_req(&req, port, enable, asCapable);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	return rc;
}  /* set_port_cfg */

int get_utc_offset(void *fd,
	int *offset)
{
	struct ksz_request_actual req;
	int rc;

	get_utc_offset_req(&req);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	if (!rc) {
		*offset = req.output;
	}
	return rc;
}  /* get_utc_offset */

int set_utc_offset(void *fd,
	int offset)
{
	struct ksz_request_actual req;
	int rc;

	set_utc_offset_req(&req, offset);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	return rc;
}  /* set_utc_offset */

int get_clk_domain(void *fd,
	u8 *clk, u8 domains[])
{
	struct ksz_request_actual req;
	int rc;

	get_clk_domain_req(&req);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	if (!rc) {
		struct ptp_clk_domains *param = (struct ptp_clk_domains *)
			&req.param;
		int i;

		*clk = param->clk;
		for (i = 0; i < param->clk; i++)
			domains[i] = param->domains[i];
	}
	return rc;
}  /* get_clk_domain */

int set_clk_domain(void *fd,
	u8 clk, u8 domains[])
{
	struct ksz_request_actual req;
	int rc;

	set_clk_domain_req(&req, clk, domains);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	return rc;
}  /* set_clk_domain */

int get_clk_domain_ports(void *fd,
	u8 *clk, u8 ports[])
{
	struct ksz_request_actual req;
	int rc;

	get_clk_domain_ports_req(&req);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	if (!rc) {
		struct ptp_clk_domains *param = (struct ptp_clk_domains *)
			&req.param;
		int i;

		*clk = param->clk;
		for (i = 0; i < param->clk; i++)
			ports[i] = param->domains[i];
	}
	return rc;
}  /* get_clk_domain_ports */

int set_clk_domain_ports(void *fd,
	u8 clk, u8 ports[])
{
	struct ksz_request_actual req;
	int rc;

	set_clk_domain_ports_req(&req, clk, ports);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	return rc;
}  /* set_clk_domain_ports */

int get_timestamp(void *fd,
	u8 msg, u8 port, u16 seqid, u8 mac[], u32 timestamp,
	u32 *sec, u32 *nsec)
{
	struct ksz_request_actual req;
	int rc;

	get_timestamp_req(&req, msg, port, seqid, mac, timestamp);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	if (!rc) {
		struct ptp_ts_options *param = (struct ptp_ts_options *)
			&req.param;

		*sec = param->sec;
		*nsec = param->nsec;
	}
	return rc;
}  /* get_timestamp */

int get_msg_info(void *fd,
	void *hdr, int *tx, u32 *port, u32 *sec, u32 *nsec)
{
	struct ksz_request_actual req;
	int rc;

	get_msg_info_req(&req, hdr, *tx);
	rc = file_dev_ioctl(fd, &req);
	if (!rc || -1 == rc)
		rc = req.result;
	if (!rc) {
		struct ptp_msg_options *param = (struct ptp_msg_options *)
			&req.param;

		*port = param->port;
		*sec = param->ts.t.sec;
		*nsec = param->ts.t.nsec;
	}
	*tx = req.output;
	return rc;
}  /* get_msg_info */

int set_msg_info(void *fd,
	void *hdr, u32 port, u32 sec, u32 nsec)
{
	struct ksz_request_actual req;
	int rc;

	set_msg_info_req(&req, hdr, port, sec, nsec);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	return rc;
}  /* set_msg_info */

int get_tail_tag(void *fd,
	void *msg, int len, u8 *tag, int *tag_len, u32 *port)
{
	struct ksz_request_actual req;
	int rc;

	get_tail_tag_req(&req, msg, len, tag, *tag_len);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	if (!rc) {
		struct ptp_msg_tag *param = (struct ptp_msg_tag *)
			&req.param;

		*port = param->ports;
		*tag_len = req.output;
	}
	return rc;
}

int set_tail_tag(void *fd,
	void *dst, void *msg, int len, uint ports, u8 queue, u8 *tag,
	int *tag_len)
{
	struct ksz_request_actual req;
	int rc;
	int have_tag = 0;

	if (tag_len)
		have_tag = 1;
	set_tail_tag_req(&req, dst, msg, len, ports, queue, have_tag);
	rc = file_dev_ioctl(fd, &req);
	if (!rc) {
		rc = req.result;
		if (!rc && tag_len) {
			*tag_len = req.output;
			if (tag)
				memcpy(tag, req.param.data, *tag_len);
		}
	}
	return rc;
}

int rx_event(void *fd,
	u8 tsi, u8 gpi, u8 event, u8 total, u8 flags, u32 timeout, int *unit)
{
	struct ksz_request_actual req;
	int rc;

	rx_event_req(&req, tsi, gpi, event, total, flags, timeout);
	rc = file_dev_ioctl(fd, &req);
	if (!rc) {
		rc = req.result;
		if (!rc && unit)
			*unit = req.output;
	}
	return rc;
}  /* rx_event */

int tx_event(void *fd,
	u8 tso, u8 gpo, u8 event, u32 pulse, u32 cycle, u16 cnt,
	u32 iterate, u32 sec, u32 nsec, u8 flags, int *unit)
{
	struct ksz_request_actual req;
	int rc;

	tx_event_req(&req, tso, gpo, event, pulse, cycle, cnt,
		iterate, sec, nsec, flags);
	rc = file_dev_ioctl(fd, &req);
	if (!rc) {
		rc = req.result;
		if (!rc && unit)
			*unit = req.output;
	}
	return rc;
}  /* tx_event */

int tx_cascade(void *fd,
	u8 tso, u8 gpo, u8 total, u16 cnt, u8 flags)
{
	struct ksz_request_actual req;
	int rc;

	tx_cascade_req(&req, tso, gpo, total, cnt, flags);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	return rc;
}  /* tx_cascade */

int tx_cascade_init(void *fd,
	u8 tso, u8 gpo, u8 total, u8 flags, int *unit)
{
	struct ksz_request_actual req;
	int rc;

	tx_cascade_init_req(&req, tso, gpo, total, flags);
	rc = file_dev_ioctl(fd, &req);
	if (!rc) {
		rc = req.result;
		if (!rc && unit)
			*unit = req.output;
	}
	return rc;
}  /* tx_cascade_init */

int tx_get_output(void *fd,
	u8 gpo, int *output)
{
	struct ksz_request_actual req;
	int rc;

	tx_get_output_req(&req, gpo);
	rc = file_dev_ioctl(fd, &req);
	if (!rc) {
		rc = req.result;
		if (!rc && output)
			*output = req.output;
	}
	return rc;
}  /* tx_get_output */

int get_rx_event(void *fd,
	u8 tsi)
{
	struct ksz_request_actual req;
	int rc;

	get_rx_event_req(&req, tsi);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	return rc;
}  /* get_rx_event */

int poll_rx_event(void *fd,
	u8 tsi)
{
	struct ksz_request_actual req;
	int rc;

	poll_rx_event_req(&req, tsi);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	return rc;
}  /* poll_rx_event */

int get_rx_event_info(void *fd,
	u8 *unit, u8 *event, u8 *num)
{
	struct ksz_request_actual req;
	int rc;

	get_rx_event_info_req(&req);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	if (!rc) {
		struct ptp_tsi_info *param = (struct ptp_tsi_info *)
			&req.param;

		*unit = param->unit;
		*event = param->event;
		*num = param->num;
	}
	return rc;
}  /* get_rx_event_info */

int rx_get_clock(void *fd,
	u8 tsi, int *output)
{
	struct ksz_request_actual req;
	int rc;

	rx_get_clock_req(&req, tsi);
	rc = file_dev_ioctl(fd, &req);
	if (!rc) {
		rc = req.result;
		if (!rc && output)
			*output = req.output;
	}
	return rc;
}  /* rx_get_clock */

int rx_set_clock(void *fd,
	u8 tsi, int clk_id)
{
	struct ksz_request_actual req;
	int rc;

	rx_set_clock_req(&req, tsi, clk_id);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	return rc;
}  /* rx_set_clock */

int tx_get_clock(void *fd,
	u8 tso, int *output)
{
	struct ksz_request_actual req;
	int rc;

	tx_get_clock_req(&req, tso);
	rc = file_dev_ioctl(fd, &req);
	if (!rc) {
		rc = req.result;
		if (!rc && output)
			*output = req.output;
	}
	return rc;
}  /* tx_get_clock */

int tx_set_clock(void *fd,
	u8 tso, int clk_id)
{
	struct ksz_request_actual req;
	int rc;

	tx_set_clock_req(&req, tso, clk_id);
	rc = file_dev_ioctl(fd, &req);
	if (!rc)
		rc = req.result;
	return rc;
}  /* tx_set_clock */

#ifdef USE_DEV_IOCTL
static int ptp_recv(struct dev_info *info, u8 data[], int len)
{
	struct ksz_read_msg *msg;
	int n;

	/* There are data left. */
	if (info->left) {
		msg = (struct ksz_read_msg *) &info->buf[info->index];

		/* But not enough. */
		if (info->left < msg->len) {
			memcpy(info->buf, &info->buf[info->index],
				info->left);
			info->index = info->left;
			info->left = 0;
		}
	} else
		info->index = 0;

	/* No more data. */
	if (!info->left) {

		/* Read from device. */
		do {
			/* This will be blocked if no data. */
			n = read(info->fd, &info->buf[info->index],
				info->len - info->index);
			if (n < 0) {
				printf("read failure\n");
				exit(1);
			}
			info->index += n;
		} while (!n && !info->index);
		info->left = info->index;
		info->index = 0;
	}
	msg = (struct ksz_read_msg *) &info->buf[info->index];
	if (msg->len > len) {
printf("  ??  %d; %d, %d %d\n", msg->len, len, info->index, info->left);
		exit(1);
	}
	info->index += msg->len;
	info->left -= msg->len;
	msg->len -= 2;

	if (len > msg->len)
		len = msg->len;
	memcpy(data, msg->data, len);
	return len;
}

static int ptp_api_init(struct dev_info *dev)
{
	char device[40];

	sprintf(device, "/dev/ptp_dev");
	if (dev->id > 0)
		snprintf(device, sizeof(device) - 1, "/dev/ptp_dev_%u",
			 dev->id);
	dev->fd = open(device, O_RDWR);
	if (dev->fd < 0) {
		printf("cannot open ptp device\n");
		return -1;
	}
	dev->len = MAX_REQUEST_SIZE;
	dev->buf = malloc(dev->len);
	dev->index = 0;
	dev->left = 0;
	return 0;
}

static void ptp_api_exit(struct dev_info *dev)
{
	if (dev->fd <= 0)
		return;
	usleep(10 * 1000);
	close(dev->fd);
	dev->fd = 0;
	free(dev->buf);
}
#endif

#include <errno.h>

int print_err(int rc)
{
	if (rc < 0) {
		switch (-rc) {
		case EAGAIN:
			break;
		case EINVAL:
			printf("  invalid value\n");
			break;
		default:
			printf("err: %d\n", rc);
		}
	} else if (rc > 0) {
		switch (rc) {
		case DEV_IOC_INVALID_SIZE:
			printf("  invalid size\n");
			break;
		case DEV_IOC_INVALID_CMD:
			printf("  invalid cmd\n");
			break;
		case DEV_IOC_INVALID_LEN:
			printf("  invalid len\n");
			break;
		case DEV_IOC_UNIT_UNAVAILABLE:
			printf("  unavailable\n");
			break;
		case DEV_IOC_UNIT_USED:
			printf("  used\n");
			break;
		case DEV_IOC_UNIT_ERROR:
			printf("  error\n");
			break;
		}
	}
	return rc;
}  /* print_err */

