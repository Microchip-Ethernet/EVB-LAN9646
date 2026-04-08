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

int rand(void);

#include "ptp_forward.h"
#include "ptp_protocol.h"

/* In ptp_manage.c */
void handle_management(struct ptp_port *p, struct ptp_message *m);
void proc_management(struct ptp_port *p, struct ptp_message *m);
/* In ptp_signal.c */
void handle_signaling(struct ptp_port *p, struct ptp_message *m);
void proc_signaling(struct ptp_port *p, struct ptp_message *m);

#define MULT_RESP_FAULT_TIMEOUT  (5 * 60 * 1000)

enum {
    CTL_SYNC,
    CTL_DELAY_REQ,
    CTL_FOLLOW_UP,
    CTL_DELAY_RESP,
    CTL_MANAGEMENT,
    CTL_OTHER,
};

int broadcast_clock_id(struct ptp_clock_identity *n)
{
    static struct ptp_clock_identity b = {{
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
    }};

    return (!memcmp(n, &b, sizeof(struct ptp_clock_identity)));
}

int same_clock_id(struct ptp_clock_identity *h,
    struct ptp_clock_identity *n)
{
    return (!memcmp(h, n, sizeof(struct ptp_clock_identity)));
}

int same_port_id(struct ptp_port_identity *h,
    struct ptp_port_identity *n)
{
    return (!memcmp(h, n, sizeof(struct ptp_port_identity)));
}

int same_port_id_net(struct ptp_port_identity *h,
    struct ptp_port_identity *n)
{
    return (!memcmp(&h->clockIdentity, &n->clockIdentity,
            sizeof(struct ptp_clock_identity)) &&
            h->port == ntohs(n->port));
}

int us_to_ptp_interval(int us)
{
    int interval, ms;

    if (us >= 1000000) {
        ms = us / 1000000;
        interval = 0;
        while (ms > (1 << interval))
            interval++;
    } else {
        interval = -4;
        ms = 62500;
        while (ms < us) {
            interval++;
            ms *= 2;
        }
    }
    return interval;
}

int ptp_interval_to_ms(int interval)
{
    int ms;

    if (interval >= 0) {

        /* Limit interval to normal range. */
        if (interval > 8)
            interval = 8;
        ms = 1 << interval;
        ms *= 1000;
    } else {
        int i;

        interval = -interval;
        for (i = 1, ms = 500000; i < interval; i++)
            ms >>= 1;
        ms /= 1000;
        if (!ms)
            ms = 1;
    }
    return ms;
}

int ptp_interval_to_cnt(int interval, int seconds)
{
    int cnt;

    if (interval >= 0) {
        cnt = 1 << interval;
        cnt = seconds / cnt;
        if (!cnt)
            cnt = 1;
    } else {
        interval = -interval;
        cnt = 1 << interval;
        cnt *= seconds;
        cnt++;
    }
    return cnt;
}

int ptp_random_ms(int ms, int percent)
{
    int random;
    int range;
    int sign;

    random = rand();
    range = ms * percent / 100;
    sign = (random >> 16) & 1;
    random %= range;
    if (sign)
        ms += random;
    else
        ms -= random;
    return ms;
}

static
void ptp_prep_hdr(struct ptp_msg_hdr *hdr, int transport, int message, int len,
    int domain, int ctrl, int interval, struct ptp_clock_identity *id)
{
    len += sizeof(struct ptp_msg_hdr);

    memset(hdr, 0, sizeof(struct ptp_msg_hdr));
    hdr->transportSpecific = transport;
    hdr->messageType = message;
    hdr->versionPTP = 2;
    hdr->messageLength = len;
    hdr->domainNumber = domain;
    hdr->flagField.flag.ptpTimescale = 1;
    memcpy(&hdr->sourcePortIdentity.clockIdentity, id,
        sizeof(struct ptp_clock_identity));
    hdr->controlField = ctrl;
    hdr->logMessageInterval = interval;
}  /* ptp_prep_hdr */

void ptp_init_announce(struct ptp_clock *c, struct ptp_msg *msg)
{
    struct ptp_msg_announce *ann;
    struct ptp_parent_ds *pds = &c->pds;
    int ctrl;
    int len;
    int log;

    ctrl = CTL_OTHER;
    len = sizeof(struct ptp_msg_announce);
    log = c->logAnnounceInterval;
    ptp_prep_hdr(&msg->hdr, c->specific, ANNOUNCE_MSG, len, c->domain, ctrl,
        log, &c->id);
    if (c->utc_offset)
        msg->hdr.flagField.flag.utcOffsetValid = 1;
    msg->hdr.sourcePortIdentity.port = c->cnt + 1;

    ann = &msg->data.announce;
    memset(&ann->originTimestamp, 0, sizeof(struct ptp_timestamp));
    ann->currentUtcOffset = c->utc_offset;
    ann->reserved = 0;
    ann->grandmasterPriority1 = pds->grandmasterPriority1;
    memcpy(&ann->grandmasterClockQuality, &pds->grandmasterClockQuality,
        sizeof(struct ptp_clock_quality));
    ann->grandmasterPriority2 = pds->grandmasterPriority2;
    memcpy(&ann->grandmasterIdentity, &pds->grandmasterIdentity,
        sizeof(struct ptp_clock_identity));
    ann->stepsRemoved = 0;
    ann->timeSource = c->time_source;
}  /* ptp_init_announce */

void ptp_init_sync(struct ptp_clock *c, struct ptp_msg *msg)
{
    int ctrl;
    int len;
    int log;

    ctrl = CTL_SYNC;
    len = sizeof(struct ptp_msg_sync);
    log = c->logSyncInterval;
    ptp_prep_hdr(&msg->hdr, c->specific, SYNC_MSG, len, c->domain, ctrl, log,
        &c->id);
    msg->hdr.sourcePortIdentity.port = c->cnt + 1;

    memset(&msg->data, 0, sizeof(struct ptp_timestamp));
}  /* ptp_init_sync */

void ptp_init_follow_up(struct ptp_clock *c, struct ptp_msg *msg)
{
    int ctrl;
    int len;
    int log;

    ctrl = CTL_FOLLOW_UP;
    len = sizeof(struct ptp_msg_follow_up);
    log = c->logSyncInterval;
    ptp_prep_hdr(&msg->hdr, c->specific, FOLLOW_UP_MSG, len, c->domain, ctrl,
        log, &c->id);
    msg->hdr.sourcePortIdentity.port = c->cnt + 1;
}  /* ptp_init_follow_up */

void ptp_init_delay_req(struct ptp_clock *c, struct ptp_msg *msg)
{
    int ctrl;
    int len;
    int log;

    ctrl = CTL_DELAY_REQ;
    len = sizeof(struct ptp_msg_sync);
    log = 0x7F;
    ptp_prep_hdr(&msg->hdr, c->specific, DELAY_REQ_MSG, len, c->domain, ctrl,
        log, &c->id);

    memset(&msg->data, 0, sizeof(struct ptp_timestamp));
}  /* ptp_init_delay_req */

void ptp_init_delay_resp(struct ptp_clock *c, struct ptp_msg *msg)
{
    int ctrl;
    int len;
    int log;

    ctrl = CTL_DELAY_RESP;
    len = sizeof(struct ptp_msg_delay_resp);
    log = 0x1;
    log = 0x0;
    ptp_prep_hdr(&msg->hdr, c->specific, DELAY_RESP_MSG, len, c->domain, ctrl,
        log, &c->id);
    msg->hdr.sourcePortIdentity.port = c->cnt + 1;
}  /* ptp_init_delay_resp */

void ptp_init_pdelay_req(struct ptp_clock *c, struct ptp_msg *msg)
{
    int ctrl;
    int len;
    int log;

    ctrl = CTL_OTHER;
    len = sizeof(struct ptp_msg_pdelay_req);
    log = 0x7F;
    ptp_prep_hdr(&msg->hdr, c->specific, PDELAY_REQ_MSG, len, c->domain, ctrl,
        log, &c->id);

    memset(&msg->data, 0, sizeof(struct ptp_msg_pdelay_req));
}  /* ptp_init_pdelay_req */

void ptp_init_manage(struct ptp_clock *c, struct ptp_msg *msg)
{
    struct ptp_msg_management *manage;
    int ctrl;
    int len;
    int log;

    ctrl = CTL_MANAGEMENT;
    len = sizeof(struct ptp_msg_management_base) +
        sizeof(struct ptp_management_error_tlv);
    log = 0x7F;
    ptp_prep_hdr(&msg->hdr, c->specific, MANAGEMENT_MSG, len, c->domain, ctrl,
        log, &c->id);
    msg->hdr.sourcePortIdentity.port = c->cnt + 1;

    manage = &msg->data.management;
    memset(&manage->b, 0, sizeof(struct ptp_msg_management_base));
    memset(&manage->b.targetPortIdentity, 0xff,
        sizeof(struct ptp_port_identity));
    manage->b.startingBoundaryHops = 0;
    manage->b.boundaryHops = 0;

    memset(&manage->tlv, 0, sizeof(struct ptp_management_error_tlv));
}  /* ptp_init_manage */

void ptp_init_signal(struct ptp_clock *c, struct ptp_msg *msg)
{
    int ctrl;
    int len;
    int log;

    ctrl = CTL_OTHER;
    len = sizeof(struct ptp_port_identity);
    log = 0x7F;
    ptp_prep_hdr(&msg->hdr, c->specific, SIGNALING_MSG, len, c->domain, ctrl,
        log, &c->id);
    msg->hdr.sourcePortIdentity.port = c->cnt + 1;

    memset(&msg->data, 0xff, sizeof(struct ptp_port_identity));
}  /* ptp_init_signal */

void ptp_utime_to_timestamp(u16 sec, struct ptp_utime *u,
    struct ptp_timestamp *t)
{
    t->sec.hi = sec;
    t->sec.lo = u->sec;
    t->nsec = u->nsec;
}  /* ptp_utime_to_timestamp */

static void clear_pdelay_req(struct ptp_port *p)
{
    struct ptp_msg_info *req = &p->tx_delay_req;
    struct ptp_msg_info *rsp = &p->rx_delay_resp;
    struct ptp_msg_info *fup = &p->rx_pdelay_fup;
    struct ptp_pdelay_info *info = NULL;
    u8 n = 1;
    int i;

    if (p->multi_pdelay) {
        rsp = p->multi_pdelay->rx_pdelay_resp;
        fup = p->multi_pdelay->rx_pdelay_fup;
        info = p->multi_pdelay->pdelay_info;
        n = MAX_PDELAY_RESP;
    }
    msg_info_clear(req);
    for (i = 0; i < n; i++) {
        if (info) {
            info->id.port = 0;
            if (rsp->valid)
                memcpy(&info->id, &rsp->id, sizeof(struct ptp_port_identity));
            info++;
        }
        msg_info_clear(rsp);
        msg_info_clear(fup);
        ++rsp;
        ++fup;
    }
}

static u8 find_msg_info(struct ptp_msg_info info[],
    u8 cnt, struct ptp_port_identity *id)
{
    struct ptp_msg_info *msg;
    u8 i;

    for (i = 0; i < cnt; i++) {
        msg = &info[i];
        if (!memcmp(&msg->id, id, sizeof(struct ptp_port_identity)))
            return i;
    }
    for (i = 0; i < cnt; i++) {
        msg = &info[i];
        if (!msg->valid)
            return i;
    }
    return cnt;
}

void port_tx_delay_req(struct ptp_port *p)
{
    struct ptp_message *m = NULL;
    struct ptp_msg *msg;

    m = alloc_msg(NULL, 100, NULL);
    if (!m)
        return;
    m = clock_new_msg(p->c, DELAY_REQ_MSG, m);
    msg = m->msg;

    msg->hdr.sequenceId = p->seqid_delay_req;
    p->seqid_delay_req++;
    msg->hdr.sourcePortIdentity.port = p->id;

    /* Save necessary information to handle transmit timestamp. */
    msg_info_save(&p->tx_delay_req, msg, NULL);

    ptp_msg_tx(msg);
    ptp_save_tx_msg(m, p->index, 1 << (p->index - 1), 7, p->c->transport,
                    p->c->vid, 0, 0);
}  /* port_tx_delay_req */

void port_add_peer_info(struct ptp_port *p, struct ptp_msg *msg)
{
    struct ptp_clock *c = p->c;
    struct ptp_msg_pdelay_req *req = &msg->data.pdelay_req;
    struct ptp_tlv *tlv = (struct ptp_tlv *)(req + 1);
    u16 len = sizeof(struct ptp_tlv);
    void *tlv_end;
    struct ptp_organization_ext_tlv *org =
        (struct ptp_organization_ext_tlv *)(req + 1);
    struct MICROCHIP_802_1AS_data_1 *peer =
        (struct MICROCHIP_802_1AS_data_1 *) org->dataField;
    struct ptp_pdelay_info *info;
    int i;
    u16 messageLength;

    org->organizationId[0] = 0x00;
    org->organizationId[1] = 0x10;
    org->organizationId[2] = 0xA1;
    org->organizationSubType[0] = 0;
    org->organizationSubType[1] = 0;
    org->organizationSubType[2] = 0x01;
    len = sizeof(struct ptp_organization_ext_tlv) - 1;
    for (i = 0; i < MAX_PDELAY_RESP; i++) {
        info = &p->multi_pdelay->pdelay_info[i];
        if (!info->id.port)
            continue;
        memcpy(&peer->respondingPortIdentity, &info->id,
               sizeof(struct ptp_port_identity));
        peer->peerDelay = (u32) info->delay;
        peer->peerRatio = float_to_ratio(info->nrate.ratio);
        peer++;
        len += sizeof(struct MICROCHIP_802_1AS_data_1);
    }

    messageLength = msg->hdr.messageLength;
    tlv_end = (peer + 1);
    tlv_end = ptp_add_tlv(tlv, tlv_end, TLV_ORGANIZATION_EXTENSION, len,
        &messageLength);
    msg->hdr.messageLength = messageLength;
}  /* port_add_peer_info */

void port_tx_pdelay_req(struct ptp_port *p)
{
    struct ptp_message *m = NULL;
    struct ptp_msg *msg;

    if (p->multi_pdelay && p->state == PTP_S_MASTER && port_capable(p)) {
        if (time_after_eq(p->last_tx_sync_jiffies + 2, jiffies)) {
            schedule_delayed_work(&p->delay_req_work, 2);
            return;
        }
    }

    /* No Pdelay_Resp received. */
    if (msg_info_valid(&p->tx_delay_req) && !p->tx_delay_req.resp) {

        /* Allowed missing responses is about 3. */
        if (p->resp_missing < 10)
            p->resp_missing++;
        p->report_resp = 1;
#ifdef DBG_PRESP_MISSING
        if (p->resp_missing < 5) {
dbg_msg("missing: %x %d %04x"NL, p->index, p->resp_missing, p->tx_delay_req.seqid);
        }
#endif
        port_check_capable(p);
#ifdef NOTIFY_PTP_EVENT
        if (p->report_pdelay && p->resp_missing == ALLOWED_LOST_RESPONSES) {
            exception_log(0, 0,
                "Pdelay response timeout at p-%u with %u missing responses",
                p->id, p->resp_missing);
        }
#endif
    }

    /* Expect only one response. */
    if (p->resp_cnt <= 1 && p->mult_resp_cnt) {
        /* Gradually reduce the multiple responses count. */
        p->mult_resp_cnt--;
    }
    p->resp_cnt = 0;
    p->resp_fup_cnt = 0;
    p->mult_resp = 0;
    clear_pdelay_req(p);

    m = alloc_msg(NULL, 100, NULL);
    if (!m)
        return;
    m = clock_new_msg(p->c, PDELAY_REQ_MSG, m);
    msg = m->msg;

    if (p->gptp)
        msg->hdr.logMessageInterval = p->ds.logMinPdelayReqInterval;
    msg->hdr.sequenceId = p->seqid_delay_req;
    p->seqid_delay_req++;
    msg->hdr.sourcePortIdentity.port = p->id;
    if (p->multi_pdelay)
        port_add_peer_info(p, msg);

    /* Save necessary information to handle transmit timestamp. */
    msg_info_save(&p->tx_delay_req, msg, NULL);

    ptp_msg_tx(msg);
    ptp_save_tx_msg(m, p->index, 1 << (p->index - 1), 7, p->c->transport,
                    p->c->vid, 0, 0);
}  /* port_tx_pdelay_req */

static int accept_domain_msg(struct ptp_port *p, struct ptp_message *m)
{
    struct ptp_msg *msg = m->msg;
    struct ptp_clock *c = get_ptp_clock(msg->hdr.domainNumber);

    /* No clock assigned to domain. */
    if (!c || c != p->c)
        return false;

    if (p->gptp && !msg->hdr.transportSpecific)
        return false;

    return true;
}  /* accept_domain_msg */

static int accept_slave_msg(struct ptp_port *p, struct ptp_message *m, int chk)
{
    struct ptp_msg *msg = m->msg;
    struct ptp_clock *c = get_ptp_clock(msg->hdr.domainNumber);

    /* No clock assigned to domain. */
    if (!c || c != p->c)
        return false;

    /* Not in slave state to accept message. */
    switch (p->state) {
    case PTP_S_UNCALIBRATED:
    case PTP_S_SLAVE:
        break;
    default:
        return false;
    }
    if (chk &&
        memcmp(&p->c->best_id, &msg->hdr.sourcePortIdentity.clockIdentity,
               sizeof(struct ptp_clock_identity)))
        return false;
    return true;
}  /* accept_slave_msg */

static int accept_seqid(int seqid, struct ptp_msg *msg)
{
    /* Initial invalid value. */
    if (seqid > 0xffff)
        return true;
    seqid = (u16)(msg->hdr.sequenceId - seqid);
    if (seqid == 0 || seqid > 0xFFF0)
        return false;
    return true;
}

static void port_rx_sync(struct ptp_port *p)
{
    if (p->ds.delayMechanism == DELAY_E2E) {
        if (p->c->servo_state != SERVO_UNLOCKED) {
            int tick = 1;

            if (p->c->servo_state == SERVO_LOCKING)
                tick = msecs_to_jiffies(500);
            ksz_start_timer(&p->delay_timer_info, tick);
        }
    }
}  /* port_rx_sync */

void *ptp_add_tlv(struct ptp_tlv *tlv, void *tlv_end, u16 type, u16 len,
    u16 *size)
{
    tlv->tlvType = type;
    if (len & 1) {
        u8 *pad = tlv_end;

        *pad = 0;
        tlv_end = pad + 1;
        len++;
    }
    *size += len;
    len -= sizeof(struct ptp_tlv);
    tlv->lengthField = len;
    return tlv_end;
}  /* ptp_add_tlv */

struct ptp_tlv *ptp_next_tlv(u8 **data, u16 *left)
{
    if (*left >= sizeof(struct ptp_tlv)) {
        struct ptp_tlv *tlv;
        u16 len;

        tlv = (struct ptp_tlv *) *data;
        len = tlv->lengthField + sizeof(struct ptp_tlv);
        if (*left < len) {
            return NULL;
        }
        *left -= len;
        *data += len;
        return tlv;
    }
    return NULL;
}

struct ptp_tlv *ptp_find_tlv(u8 *data, u16 type, u16 *left, u8 **last)
{
    struct ptp_tlv *tlv;
    u16 len;

    while (*left >= sizeof(struct ptp_tlv)) {
        tlv = (struct ptp_tlv *) data;
        len = tlv->lengthField + sizeof(struct ptp_tlv);
        if (*left < len) {
            return NULL;
        }
        *left -= len;
        if (tlv->tlvType == type)
            return tlv;
        data += len;
    }
    if (last)
        *last = data;
    return NULL;
}  /* ptp_find_tlv */

static void check_follow_up(struct ptp_clock *c, struct ptp_msg *msg)
{
    struct ptp_tlv *tlv;
    u8 *data;
    u16 size;
    u16 len;

    size = sizeof(struct ptp_msg_hdr) + sizeof(struct ptp_msg_follow_up);
    len = msg->hdr.messageLength;
    if (len > size) {
        len -= size;
        data = (u8 *) &msg->hdr;
        data += size;
        tlv = ptp_find_tlv(data, TLV_ORGANIZATION_EXTENSION, &len, NULL);
        if (tlv) {
            struct ptp_organization_ext_tlv *org =
                (struct ptp_organization_ext_tlv *)tlv;

            if (org->organizationId[0] == 0x00 &&
                org->organizationId[1] == 0x80 &&
                org->organizationId[2] == 0xC2 &&
                org->organizationSubType[0] == 0x00 &&
                org->organizationSubType[1] == 0x00 &&
                org->organizationSubType[2] == 0x01) {
                struct IEEE_802_1AS_data_1 *data =
                    (struct IEEE_802_1AS_data_1 *) org->dataField;

                        double r = ratio_to_float(data->cumulativeScaledRateOffset);

                        /* Need to implement code to use this information. */
#if 0
                        memset(&data->lastGmPhaseChange, 0, 12);
                        data->scaledLastGmFreqChange = (0);
#endif
            }
        }
    }
}  /* check_follow_up */

static void check_peer_info(struct ptp_port *p, struct ptp_msg *msg, u8 *src)
{
    struct ptp_clock *c = p->c;
    struct ptp_tlv *tlv;
    u8 *data;
    u16 size;
    u16 len;

    size = sizeof(struct ptp_msg_hdr) + sizeof(struct ptp_msg_pdelay_req);
    len = msg->hdr.messageLength;
    if (len > size) {
        len -= size;
        data = (u8 *) &msg->hdr;
        data += size;
        tlv = ptp_find_tlv(data, TLV_ORGANIZATION_EXTENSION, &len, NULL);
        if (tlv) {
            struct ptp_organization_ext_tlv *org =
                (struct ptp_organization_ext_tlv *)tlv;

            if (org->organizationId[0] == 0x00 &&
                org->organizationId[1] == 0x10 &&
                org->organizationId[2] == 0xA1) {
                struct MICROCHIP_802_1AS_data_1 *peer =
                    (struct MICROCHIP_802_1AS_data_1 *) org->dataField;
                int i = tlv->lengthField + sizeof(struct ptp_tlv);
                int cnt = 1;

                i -= sizeof(struct ptp_organization_ext_tlv) - 1;
                while (i >= sizeof(struct MICROCHIP_802_1AS_data_1)) {
                    if (!memcmp(&peer->respondingPortIdentity.clockIdentity,
                                &c->id, sizeof(struct ptp_clock_identity)) &&
                        peer->respondingPortIdentity.port == p->id) {
                        struct ptp_pdelay_info *info = &p->pdelay_info;
                        double r = ratio_to_float(peer->peerRatio);
                        u8 addr[6];
                        int n, num;

                        info->delay = peer->peerDelay;
                        info->raw_delay = info->delay;
                        info->nrate.ratio = r;
                        p->peer_index = cnt;

                        /* There is a source MAC address. */
                        if (src[0] != 0xFF) {
                            addr[0] = msg->hdr.sourcePortIdentity.clockIdentity.addr[0];
                            addr[1] = msg->hdr.sourcePortIdentity.clockIdentity.addr[1];
                            addr[2] = msg->hdr.sourcePortIdentity.clockIdentity.addr[2];
                            addr[3] = msg->hdr.sourcePortIdentity.clockIdentity.addr[5];
                            addr[4] = msg->hdr.sourcePortIdentity.clockIdentity.addr[6];
                            addr[5] = msg->hdr.sourcePortIdentity.clockIdentity.addr[7];
                            memcpy(p->peer_addr, addr, 6);
                            n = 5;
                            num = addr[5] + msg->hdr.sourcePortIdentity.port;
                            addr[5] = (u8)num;
                            while (num > 0xff && n >= 3) {
                                addr[n] = num - 255;
                                n--;
                                num = addr[n] + 1;
                            }
                            if (memcmp(addr, src, 6))
                                p->peer_addr[0] = 0xFF;
                        }
                        port_assign_peer_delay(p, info);
                        break;
                    }
                    cnt++;
                    peer++;
                    i -= sizeof(struct MICROCHIP_802_1AS_data_1);
                }
            }
        }
    }
}  /* check_peer_info */

void clock_add_path_trace(struct ptp_clock *c, struct ptp_msg *msg)
{
    struct ptp_msg_announce *ann = &msg->data.announce;
    u8 *data = (u8 *)(ann + 1);
    struct ptp_tlv *tlv;
    void *tlv_end;
    u16 len;
    u16 left;
    u16 size;
    u8 *last;
    u16 messageLength;
    struct ptp_clock_identity *id = NULL;

    size = sizeof(struct ptp_msg_hdr) + sizeof(struct ptp_msg_announce);
    len = msg->hdr.messageLength;
    data = (u8 *) &msg->hdr;
    data += size;
    tlv = (struct ptp_tlv *) data;
    if (len > size) {
        len -= size;
        tlv = ptp_find_tlv(data, TLV_PATH_TRACE, &len, &last);
        if (tlv) {
            u16 cnt = tlv->lengthField / sizeof(struct ptp_clock_identity);

            if (cnt * sizeof(struct ptp_clock_identity) != tlv->lengthField) {
dbg_msg(" cnt not match"NL);
                return;
            }
            id = (struct ptp_clock_identity *)(tlv + 1);
            id += cnt;
            len = tlv->lengthField + sizeof(struct ptp_tlv);
            msg->hdr.messageLength -= len;
        } else {
            if (len > 2) {
dbg_msg(" other tlv found?"NL);
                return;
            }
            tlv = (struct ptp_tlv *) last;
        }
    }

    /* No path trace present. */
    if (!id) {
        id = (struct ptp_clock_identity *)(tlv + 1);
        len = sizeof(struct ptp_tlv);
    }
    memcpy(id, &c->id, sizeof(struct ptp_clock_identity));
    len += sizeof(struct ptp_clock_identity);
    tlv_end = (id + 1);
    messageLength = msg->hdr.messageLength;
    tlv_end = ptp_add_tlv(tlv, tlv_end, TLV_PATH_TRACE, len,
        &messageLength);
    msg->hdr.messageLength = messageLength;
}  /* clock_add_path_trace */

void port_fwd_announce(struct ptp_port *p, struct ptp_message *m)
{
    struct ptp_message *ann;
    struct ptp_msg *msg;
    int len;

    ann = clock_get_msg(p->c, m->msg, NULL);
    msg = ann->msg;

    msg->data.announce.stepsRemoved++;
    if (p->path_trace) {
        clock_add_path_trace(p->c, msg);
    }

    ptp_msg_tx(msg);
    ptp_tx_msg(ann, p->index, p->portmap, 7, p->c->transport, p->c->vid,
               0, 0, NULL);
}  /* port_fwd_announce */

void port_tx_announce(struct ptp_port *p)
{
    struct ptp_message *ann = NULL;
    struct ptp_msg *msg;

    if (p->state != PTP_S_MASTER)
        return;

    /* Need as_capable to send. */
    if (!port_capable(p))
        return;

#ifdef TEST_TX_SYNC
if (dbg_tx_sync > 20)
    return;
#endif
    ann = alloc_msg(NULL, 100, NULL);
    if (!ann)
        return;
    ann = clock_new_msg(p->c, ANNOUNCE_MSG, ann);
    msg = ann->msg;

    msg->hdr.sequenceId = p->seqid_ann;
    p->seqid_ann++;
    if (!p->no_id_replace)
        msg->hdr.sourcePortIdentity.port = p->id;
    if (p->path_trace)
        clock_add_path_trace(p->c, msg);

    ptp_msg_tx(msg);
    ptp_save_tx_msg(ann, p->index, p->portmap, 7, p->c->transport, p->c->vid,
                    0, 0);
}  /* port_tx_announce */

void port_fwd_sync(struct ptp_port *p, struct ptp_message *m)
{
    struct ptp_message *sync;
    struct ptp_msg *msg;

    /* Send message in different interval. */
    if (p->cnt_sync_tx) {
        p->cnt_sync_tx--;
        if (!p->cnt_sync_tx) {
            p->cnt_sync_tx = p->max_sync_tx;
        }
        if (p->cnt_sync_tx != p->max_sync_tx)
            return;
    }

    sync = clock_get_msg(p->c, m->msg, NULL);
    msg = sync->msg;

/* Workaround for test case. */
#if 1
    msg->hdr.sourcePortIdentity.port = p->id;
#endif
    msg->hdr.logMessageInterval = p->logSyncInterval;

    /* Save necessary information to handle transmit timestamp. */
    msg_info_save(&p->tx_sync, msg, NULL);

    ptp_msg_tx(msg);
    ptp_tx_msg(sync, p->index, 1 << (p->index - 1), 7, p->c->transport,
               p->c->vid, 0, 0, NULL);
}  /* port_fwd_sync */

void port_fwd_fup(struct ptp_port *p, struct ptp_message *m, s64 residence)
{
    struct ptp_message *fup;
    struct ptp_msg *msg;
    s64 rx;

    fup = clock_get_msg(p->c, m->msg, NULL);
    msg = fup->msg;

    rx = msg->hdr.correctionField;
    rx += residence;
    msg->hdr.correctionField = rx;
    msg->hdr.logMessageInterval = p->logSyncInterval;
/* Workaround for test case. */
#if 1
    msg->hdr.sourcePortIdentity.port = p->id;
#endif

    msg_info_clear(&p->tx_sync);

    ptp_msg_tx(msg);
    ptp_tx_msg(fup, p->index, 1 << (p->index - 1), 7, p->c->transport,
               p->c->vid, 0, 0, NULL);
}  /* port_fwd_fup */

static void ptp_set_org_ext_tlv(struct ptp_organization_ext_tlv *org, u8 sub)
{
    org->organizationId[0] = 0x00;
    org->organizationId[1] = 0x80;
    org->organizationId[2] = 0xC2;
    org->organizationSubType[0] = 0;
    org->organizationSubType[1] = 0;
    org->organizationSubType[2] = sub;
}

void clock_add_follow_up(struct ptp_clock *c, struct ptp_msg *msg)
{
    struct ptp_msg_follow_up *fup = &msg->data.follow_up;
    struct ptp_tlv *tlv = (struct ptp_tlv *)(fup + 1);
    u16 len = sizeof(struct ptp_tlv);
    void *tlv_end;
    struct ptp_organization_ext_tlv *org =
        (struct ptp_organization_ext_tlv *)(fup + 1);
    struct IEEE_802_1AS_data_1 *data =
        (struct IEEE_802_1AS_data_1 *) org->dataField;
    u16 messageLength;

    ptp_set_org_ext_tlv(org, 1);

    data->cumulativeScaledRateOffset = (0);
    data->gmTimeBaseIndicator = (0);
    memset(&data->lastGmPhaseChange, 0, 12);
    data->scaledLastGmFreqChange = (0);

    tlv_end = (data + 1);
    len = sizeof(struct ptp_organization_ext_tlv) - 1 +
        sizeof(struct IEEE_802_1AS_data_1);
    messageLength = msg->hdr.messageLength;
    tlv_end = ptp_add_tlv(tlv, tlv_end, TLV_ORGANIZATION_EXTENSION, len,
        &messageLength);
    msg->hdr.messageLength = messageLength;
}  /* clock_add_follow_up */

void port_tx_sync(struct ptp_port *p)
{
    struct ptp_message *sync = NULL;
    struct ptp_msg *msg;

    if (p->state != PTP_S_MASTER)
        return;

    /* Need as_capable to send. */
    if (!port_capable(p))
        return;

#ifdef TEST_TX_SYNC
++dbg_tx_sync;
if (dbg_tx_sync > 50)
    dbg_tx_sync = 0;
if (dbg_tx_sync > 20)
    return;
#endif

    /* Send message in different interval. */
    if (p->index) {
        if (p->cnt_sync_tx) {
            p->cnt_sync_tx--;
            if (!p->cnt_sync_tx) {
                p->cnt_sync_tx = p->max_sync_tx;
            }
            if (p->cnt_sync_tx != p->max_sync_tx)
                return;
        }
    }

    sync = alloc_msg(NULL, 100, NULL);
    if (!sync)
        return;
    sync = clock_new_msg(p->c, SYNC_MSG, sync);
    msg = sync->msg;

    msg->hdr.sequenceId = p->seqid_sync;
    p->seqid_sync++;
    if (!p->no_id_replace) {
        msg->hdr.sourcePortIdentity.port = p->id;
        msg->hdr.logMessageInterval = p->logSyncInterval;
    }

    if (p->sync_two_step) {
        msg->hdr.flagField.flag.twoStepFlag = 1;

        /* Save necessary information to handle transmit timestamp. */
        msg_info_save(&p->tx_sync, msg, NULL);
    }

    ptp_msg_tx(msg);
    ptp_save_tx_msg(sync, p->index, p->portmap, 7, p->c->transport, p->c->vid,
                    0, 0);
    if (p->multi_pdelay) {
        p->last_tx_sync_jiffies = jiffies;
    }
}  /* port_tx_sync */

static void port_tx_fup(struct ptp_port *p, struct ptp_message *m)
{
    struct ptp_message *fup = NULL;
    struct ptp_msg *msg;

    fup = alloc_msg(NULL, 100, NULL);
    if (!fup)
        return;
    fup = clock_new_msg(p->c, FOLLOW_UP_MSG, fup);
    msg = fup->msg;

    msg->hdr.sequenceId = m->msg->hdr.sequenceId;
    if (!p->no_id_replace) {
        msg->hdr.sourcePortIdentity.port = p->id;
        msg->hdr.logMessageInterval = p->logSyncInterval;
    }
    ptp_utime_to_timestamp(0, &m->rx,
        &msg->data.follow_up.preciseOriginTimestamp);

    if (p->follow_up_tlv)
        clock_add_follow_up(p->c, msg);

    msg_info_clear(&p->tx_sync);

    ptp_msg_tx(msg);
    ptp_save_tx_msg(fup, p->index, 1 << (p->index - 1), 7, p->c->transport,
                    p->c->vid, 0, 0);
}  /* port_tx_fup */

static struct ptp_message *port_tx_signal(struct ptp_port *p,
    struct ptp_port_identity *id, struct ptp_message *signal)
{
    struct ptp_msg *msg;

    signal = clock_new_msg(p->c, SIGNALING_MSG, signal);
    msg = signal->msg;

    msg->hdr.sequenceId = p->seqid_signal;
    p->seqid_signal++;
    if (!p->no_id_replace)
        msg->hdr.sourcePortIdentity.port = p->id;

    if (id)
        memcpy(&msg->data.signaling.b, id, sizeof(struct ptp_port_identity));
    return signal;
}  /* port_tx_signal */

void port_tx_signal_interval(struct ptp_port *p, int delay, int sync, int ann,
    struct ptp_message *m)
{
    struct ptp_message *signal;
    struct ptp_organization_ext_tlv *org;
    struct IEEE_802_1AS_data_2 *data;
    struct ptp_msg *msg;
    void *tlv_end;
    u16 len;
    u16 messageLength;

    signal = port_tx_signal(p, NULL, m);
    msg = signal->msg;

    org = &msg->data.signaling.tlv.org_ext;
    ptp_set_org_ext_tlv(org, 2);

    data = (struct IEEE_802_1AS_data_2 *) org->dataField;
    data->linkDelayInterval = (signed char) delay;
    data->timeSyncInterval = (signed char) sync;
    data->announceInterval = (signed char) ann;
#if 0
    data->announceInterval = 0;
#endif
    data->flags = 0x3;
    data->reserved = 0;

    tlv_end = (data + 1);
    len = sizeof(struct ptp_organization_ext_tlv) - 1 +
        sizeof(struct IEEE_802_1AS_data_2);
    messageLength = msg->hdr.messageLength;
    tlv_end = ptp_add_tlv(&org->tlv, tlv_end, TLV_ORGANIZATION_EXTENSION,
        len, &messageLength);
    msg->hdr.messageLength = messageLength;

    ptp_msg_tx(msg);

    /* Not invoked in received message processing. */
    if (m) {
        ptp_save_tx_msg(signal, p->index, 1 << (p->index - 1), 7,
                        p->c->transport, p->c->vid, 0, 0);
        return;
    }
    ptp_tx_msg(signal, p->index, 1 << (p->index - 1), 7, p->c->transport,
               p->c->vid, 0, 0, NULL);
}  /* port_tx_signal_interval */

static void handle_delayed_sync(struct ptp_port *p, struct ptp_message *m)
{
    struct ptp_port *h = clock_host_port(p->c);
    struct ptp_msg_info *sync = &p->tx_sync;

    if (!msg_info_valid(sync)) {

        /* Host port may send the Sync in E2E TC. */
        sync = &h->tx_sync;
        if (h->state != PTP_S_MASTER || !msg_info_valid(sync))
            return;
    }

    /* Using 1-step. */
    if (!p->sync_two_step)
        return;

    if (sync->seqid != m->msg->hdr.sequenceId)
        return;

    /* Save the transmit timestamp. */
    memcpy(&sync->hw_time, &m->rx, sizeof(struct ptp_utime));

    ptp_save_rx_msg(m);
}  /* handle_delayed_sync */

static void proc_delayed_sync(struct ptp_port *p, struct ptp_message *m)
{
    struct ptp_port *h = clock_host_port(p->c);
    struct ptp_msg_info *sync = &p->tx_sync;

    tx_msg_lock();

    /* Follow_Up is already sent. */
    if (!msg_info_valid(sync)) {

        /* Host port may send the Sync in E2E TC. */
        sync = &h->tx_sync;
        if (h->state != PTP_S_MASTER || !msg_info_valid(sync))
            goto done;
    }

    /* Need to check again for Sync sent by host. */
    if (sync == &h->tx_sync) {

        /* Using 1-step. */
        if (!p->sync_two_step)
            goto done;

        if (sync->seqid != m->msg->hdr.sequenceId)
            goto done;

        /* Save the transmit timestamp. */
        memcpy(&sync->hw_time, &m->rx, sizeof(struct ptp_utime));
    }

    /* This is a grandmaster clock. */
    if (h->state == PTP_S_MASTER) {
        port_tx_fup(p, m);
    } else {
        struct ptp_clock *c = p->c;

        /* Forward Follow_Up if received. */
        if (c->last_fup)
            ptp_fwd_fup(p, c->last_fup);
    }

done:
    tx_msg_unlock();
}  /* proc_delayed_sync */

static void handle_sync(struct ptp_port *p, struct ptp_message *m)
{
    struct ptp_msg *msg = m->msg;
    struct ptp_clock *c = get_ptp_clock(msg->hdr.domainNumber);
    bool forward = true;
    bool accept = true;

    if (port_is_aed_master(p)) {
#ifdef NOTIFY_PTP_EVENT
        /* Report once when not supposed to receive this message. */
        if (p->report_sync) {
            p->report_sync = 0;
            exception_log(0, 0, "Sync %04x received at p-%u",
                          msg->hdr.sequenceId, p->id);
        }
#endif
        return;
    }
    if (port_is_aed(p))
        accept = false;
    if (!accept_slave_msg(p, m, accept))
        return;

    /* Accept only sequence id in advance. */
    if (!accept_seqid(c->rx_sync_seqid, msg))
        return;

    /* Accept whatever master clock in Automotive profile. */
    if (port_is_aed(p))
        memcpy(&c->best_id, &msg->hdr.sourcePortIdentity.clockIdentity,
               sizeof(struct ptp_clock_identity));

    /* Reset interval timer as soon as possible. */
    if (p->gptp) {
        c->prev_sync_jiffies = c->last_sync_jiffies;
        time_save(&c->last_sync_jiffies, 1);
        clock_set_sync_interval(c);
        if (msg->hdr.flagField.flag.twoStepFlag)
            clock_set_fup_timeout(c);
        else
            c->rx_fup_seqid = msg->hdr.sequenceId;

        /* Do not forward if Follow_Up is sent in timeout execution. */
        if (c->miss_sync && c->tx_sync_seqid == msg->hdr.sequenceId)
            forward = false;

        /* Follow_Up is received so restart sync timeout, which can exit
         * Sync timeout state.
         */
        if (c->rx_fup_seqid == c->rx_sync_seqid)
            clock_set_sync_timeout(c);

        if (c->sync_timeout || c->tx_sync)
            forward = false;

        /* If Sync is not forwarded then Follow_Up is also not. */
        if (!forward)
            c->miss_fup = 1;

        /* Clear indication to allow Follow_Up to be forwarded. */
        else
            c->miss_fup = 0;

        if (forward)
            ptp_forward(p, m);

        if (p->gptp)
            ptp_save_sync(c, m);

        if (!c->sync_timeout)
            c->miss_sync = 0;
#ifdef DEBUG_MSG
        if (!forward)
dbg_msg(" no fwd: %d %d %04x %lu"NL, c->tx_sync, c->miss_sync,
    msg->hdr.sequenceId, jiffies);
#endif
    }
    c->rx_sync_seqid = msg->hdr.sequenceId;

    msg_info_save(&c->rx_sync, msg, &m->rx);
    ptp_save_rx_msg(m);
}  /* handle_sync */

static int dbg_intv;
static void proc_sync_interval(struct ptp_port *p, struct ptp_message *m)
{
    struct ptp_msg *msg = m->msg;
    struct ptp_clock *c = p->c;
    struct ptp_port *h = clock_host_port(p->c);
    int interval = msg->hdr.logMessageInterval;
    int ms = ptp_interval_to_ms(interval);
    int actual, diff;

    if (!c->prev_sync_jiffies)
        return;
    actual = time_delta(c->prev_sync_jiffies, c->last_sync_jiffies);
    diff = actual - ms;
    if (abs(diff) <= ms / 4) {
        p->report_mismatched = 0;
    } else {
#if 0
/* gPTP_6.1.4d */
interval = -4;
#endif
if (dbg_intv < 8) {
dbg_msg("%lu %lu %04x %d\n", c->prev_sync_jiffies, c->last_sync_jiffies,
msg->hdr.sequenceId, actual);
}
++dbg_intv;
        if (p->report_mismatched < 3)
            p->report_mismatched++;
        if (p->report_mismatched >= 2) {
#ifdef NOTIFY_PTP_EVENT
            if (p->report_mismatched == 2)
                exception_log(0, 0,
                    "logMessageInterval is %d while actual interval is %d ms",
                    interval, actual);
#endif
            interval = us_to_ptp_interval(actual);
        }
    }
    
    if (c->logSyncInterval != interval) {
        int delay = ptp_interval_to_ms(interval);

        /* Need to update Sync timer delay in case of Sync timeout. */
        c->sync_timer_info.period = msecs_to_jiffies(delay);
        c->logSyncInterval = interval;

        /* This number is not valid if the intervals do not match. */
        p->cnt_sync_rx = 0;
        h->cnt_sync_rx = 5;

        /* Need to send AVB Sync when a new interval is used. */
        c->first_sync = false;
        c->report_sync = true;
#ifdef NOTIFY_PTP_EVENT
        exception_log(0, 0, "Sync interval is changed to %d", interval);
#endif
    }
    if (h->logSyncInterval != interval) {
        bool sending_signaling = p->c->signaling;

        if (!sending_signaling) {
            int new_interval = h->logSyncInterval;

            h->logSyncInterval = interval;
            ptp_change_slave_interval(p->c, new_interval, false);
        }
    } else {
        if (port_is_aed(p) && c->req_interval) {
            c->req_interval = false;

            /* Timer for operational Sync interval is not needed. */
            if (c->signaling && !c->oper_sync_jiffies)
                clock_stop_signaling_timer(c);
        }

        /* Have different operational Sync interval and the timer is not yet
         * set.
         */
        if (p->logSyncInterval != p->operSyncInterval &&
            !c->oper_sync_jiffies) {
            time_save(&c->oper_sync_jiffies, 1);
            if (!c->signaling)
                clock_start_signaling_timer(c, true);
        }
    }
}

static void proc_sync(struct ptp_port *p, struct ptp_message *m)
{
    struct ptp_msg *msg = m->msg;
    struct ptp_clock *c = get_ptp_clock(msg->hdr.domainNumber);

    if (port_is_aed(p))
        proc_sync_interval(p, m);

    /* Process Sync if one-step. */
    if (!msg->hdr.flagField.flag.twoStepFlag) {
        port_sync(p, &m->rx, &msg->data.sync.originTimestamp,
            msg->hdr.correctionField, 0, msg->hdr.sequenceId);
        port_rx_sync(p);
        return;
    }

    /* Wait for Follow_Up. */
    if (msg_info_valid(&c->rx_fup)) {
        int found = 0;

        if (c->rx_fup.seqid ==
            msg->hdr.sequenceId) {
            port_sync(p, &m->rx,
                      &c->rx_fup.ptp_time,
                      msg->hdr.correctionField,
                      c->rx_fup.correction, msg->hdr.sequenceId);
            port_rx_sync(p);
            found = 1;
        }
        msg_info_clear(&c->rx_fup);
        if (found)
            return;
    }

    /* Save 2-step Sync to match Follow_Up. */
    msg_info_save(&c->rx_sync, msg, &m->rx);
}  /* proc_sync */

static void handle_fup(struct ptp_port *p, struct ptp_message *m)
{
    struct ptp_msg *msg = m->msg;
    struct ptp_clock *c = get_ptp_clock(msg->hdr.domainNumber);
    bool forward = false;

    if (port_is_aed_master(p)) {
        return;
    }
    if (!accept_slave_msg(p, m, true))
        return;

    /* Accept only sequence id in advance. */
    if (!accept_seqid(c->rx_fup_seqid, msg))
        return;

    if (msg_info_valid(&c->rx_sync)) {
        int found = 0;

        if (c->rx_sync.seqid == msg->hdr.sequenceId) {

            /* Indicate Follow_Up is received so restart sync timeout. */
            if (p->gptp) {
                clock_stop_fup_timeout(c);
            }
            found = 1;
        }
        if (found)
            goto done;
    }
    msg_info_save(&c->rx_fup, msg, NULL);

done:
    check_follow_up(c, msg);

    /* Forward if Follow_Up is not sent in timeout execution. */
    if (p->gptp) {
        uint diff = 0;

        forward = true;

        /* Follow_Up may be sent in timeout execution, or associated Sync was
         * not forwarded, or Sync was not received.
         */
        if (c->miss_fup || c->rx_sync_seqid != msg->hdr.sequenceId)
            forward = false;
        if (c->sync_timeout || c->tx_sync)
            forward = false;

        diff = time_delta(c->set_sync_timeout_jiffies, jiffies);

        /* When the Sync timeout timer is not reset when receiving Sync
         * because the previous Sync/Follow_Up pair are not received then it
         * is necessary to reset here when the delay from last reset is long
         * enough.  This can exit the Sync timeout state.
         * This can be used to set the timer the very first time in debug
         * situation.
         */
        if ((diff >= 2 * ptp_interval_to_ms(p->logSyncInterval) &&
            c->rx_sync_seqid == msg->hdr.sequenceId) ||
            (c->rx_fup_seqid > 0xffff && c->rx_sync_seqid == 0x10000))
            clock_set_sync_timeout(c);
    }
    c->rx_fup_seqid = msg->hdr.sequenceId;
    if (forward)
        ptp_forward(p, m);
    if (p->gptp)
        ptp_save_fup(c, m);
    c->miss_fup = 0;

    ptp_save_rx_msg(m);
}  /* handle_fup */

static void proc_fup(struct ptp_port *p, struct ptp_message *m)
{
    struct ptp_msg *msg = m->msg;
    struct ptp_clock *c = get_ptp_clock(msg->hdr.domainNumber);

    if (msg_info_valid(&c->rx_sync)) {
        if (c->rx_sync.seqid == msg->hdr.sequenceId) {
            port_sync(p, &c->rx_sync.hw_time,
                      &msg->data.follow_up.preciseOriginTimestamp,
                      c->rx_sync.correction,
                      msg->hdr.correctionField, msg->hdr.sequenceId);
            port_rx_sync(p);
        }
        msg_info_clear(&c->rx_sync);
    }
}  /* proc_fup */

static int dataset_cmp(struct bmc_dataset *old, struct bmc_dataset *new)
{
    int len;

    /* Only compare parts of dataset information. */
    len = sizeof(struct bmc_dataset) - sizeof(struct ptp_port_identity) * 2;
    return memcmp(old, new, len);
}

static void announce_to_dataset(struct ptp_msg *msg, struct bmc_dataset *ds,
    struct ptp_port *p)
{
    struct ptp_msg_announce *ann = &msg->data.announce;

    /* Convert Announce information to dataset for easy comparison. */
    ds->prio_1 = ann->grandmasterPriority1;
    ds->prio_2 = ann->grandmasterPriority2;
    ds->steps_removed = ann->stepsRemoved;
    memcpy(&ds->id, &ann->grandmasterIdentity,
        sizeof(struct ptp_clock_identity));
    memcpy(&ds->quality, &ann->grandmasterClockQuality,
        sizeof(struct ptp_clock_quality));
    memcpy(&ds->tx, &msg->hdr.sourcePortIdentity,
        sizeof(struct ptp_port_identity));
    memcpy(&ds->rx, &p->c->id, sizeof(struct ptp_clock_identity));
    ds->rx.port = p->id;
}  /* announce_to_dataset */

static int add_foreign_master(struct ptp_port *p, struct bmc_dataset *ds)
{
    struct foreign_dataset *f;
    int threshold = FOREIGN_MASTER_THRESHOLD;
    int meet_threshold = 0;
    int diff = 0;

    if (p->gptp)
        threshold = 1;
    f = find_foreign_master(p->foreign_masters, ds);

    /* Cannot store too many Announce. */
    if (!f)
        return 0;

    port_set_announce_timeout(p);

    /* This is a new one. */
    if (!f->cnt) {
        f->port = p;
    }
    if (threshold - 1 == f->cnt)
        meet_threshold = 1;
    if (f->cnt > 1) {
        diff = dataset_cmp(&f->dataset, ds);
        if (diff)
            memcpy(&f->dataset, ds, sizeof(struct bmc_dataset));
    }
    if (!f->valid)
        f->cnt++;
    if (meet_threshold)
        f->valid = 1;

    /* Need decision if significant or not same. */
    return meet_threshold || diff;
}

static int update_current_master(struct ptp_port *p, struct bmc_dataset *ds)
{
    struct foreign_dataset *f = p->best;
    int diff = 0;

    /* Add new one if not matched with the one being used. */
    if (!same_port_id(&ds->tx, &f->dataset.tx))
        return add_foreign_master(p, ds);

    /* Port in PTP_S_PASSIVE needs Announce timeout to become master.
     * Port in PTP_S_SLAVE can become faulty or disabled and shutdown
     * everything.
     * Need Announce timeout in the always running host port to signal state
     * change.
     */
    port_set_announce_timeout(p);
    if (p->state != PTP_S_PASSIVE)
        port_set_announce_timeout(clock_host_port(p->c));

    if (f->cnt) {
        diff = dataset_cmp(&f->dataset, ds);
        if (diff) {
            memcpy(&f->dataset, ds, sizeof(struct bmc_dataset));
            ptp_update_parent_ds(p->c, f);
        }
    }

    /* No change for state decision unless different. */
    return diff;
}

void handle_state_decision(struct ptp_clock *c)
{
    struct ptp_port *p;
    struct foreign_dataset *best = NULL;
    struct foreign_dataset *f;
    struct bmc_dataset *clock_own = clock_default_ds(c);
    struct ptp_clock_identity best_id;
    int i;
    int event;
    int state;
    bool have_slave = false;
    bool have_master = false;

    if (c->no_announce)
        return;

    for (i = 1; i <= c->cnt; i++) {
        p = c->ptp_ports[i];
        p->best = find_best_master(p->foreign_masters);
        f = p->best;
        if (!f)
            continue;
        if (!best || bmca_better(&f->dataset, &best->dataset) > 0)
            best = f;
    }
    if (best && bmca_better(&best->dataset, clock_own) > 0) {
        c->best = best;
        memcpy(&best_id, &best->dataset.id,
            sizeof(struct ptp_clock_identity));
    } else {
        c->best = NULL;
        memcpy(&best_id, &c->dds.clockIdentity,
            sizeof(struct ptp_clock_identity));
    }

    /* Grandmaster clock is changed. */
    if (memcmp(&best_id, &c->best_id, sizeof(struct ptp_clock_identity))) {
        memcpy(&c->best_id, &best_id, sizeof(struct ptp_clock_identity));
        ptp_update_parent_ds(c, c->best);

        /* Initialize to invalid values so Sync can be accepted from new
         * master clock.
         */
        c->rx_sync_seqid = 0x30000;
        c->rx_fup_seqid = 0x30001;
    }

    for (i = 1; i <= c->cnt; i++) {
        p = c->ptp_ports[i];
        state = bmca_decision(c, p);
        if (state != p->last_state)
dbg_msg("bmca state: %x,%x=%d"NL, i, p->index, state);
        switch (state) {
        case PTP_S_LISTENING:
            event = EVENT_NONE;
            break;
        case PTP_S_PRE_MASTER:
            event = EVENT_READY_MASTER;
            break;
        case PTP_S_MASTER:
            event = EVENT_BECOME_MASTER;
            have_master = true;
            break;
        case PTP_S_PASSIVE:
dbg_msg("  become passive: %x"NL, p->index);
            event = EVENT_BECOME_PASSIVE;
            break;
        case PTP_S_SLAVE:
            event = EVENT_BECOME_SLAVE;
            have_slave = true;
            break;
        default:
            event = EVENT_FAULT_SET;
            break;
        }
        p->last_state = state;
        state = port_event(p, event);
        port_set_state(p, state);
        if (p->state == PTP_S_MASTER)
            have_master = true;
    }

    p = c->ptp_ports[0];
    if (have_slave)
        event = EVENT_BECOME_SLAVE;
    else if (have_master)
        event = EVENT_BECOME_MASTER;
    else
        event = EVENT_BECOME_PASSIVE;
    state = host_event(p, event);
    port_set_state(p, state);
}  /* handle_state_decision */

static int check_path_trace(struct ptp_clock *c, struct ptp_msg *msg)
{
    u8 *data;
    struct ptp_tlv *tlv;
    int found = 0;
    u16 size;
    u16 len;

    size = sizeof(struct ptp_msg_hdr) + sizeof(struct ptp_msg_announce);
    len = msg->hdr.messageLength;
    if (len > size) {
        len -= size;
        data = (u8 *) &msg->hdr;
        data += size;
        tlv = ptp_find_tlv(data, TLV_PATH_TRACE, &len, NULL);
        if (tlv) {
            struct ptp_clock_identity *id = (struct ptp_clock_identity *)
                (tlv + 1);
            len = tlv->lengthField;
            while (len >= sizeof(struct ptp_clock_identity)) {
                if (!memcmp(id, &c->id, sizeof(struct ptp_clock_identity))) {
                    found = 1;
                    break;
                }
                id++;
                len -= sizeof(struct ptp_clock_identity);
            }
        }
    }
    return found;
}  /* check_path_trace */

static void handle_announce(struct ptp_port *p, struct ptp_message *m)
{
    struct ptp_msg *msg = m->msg;
    struct bmc_dataset ds;
    int result = 0;

    if (port_is_aed(p)) {
#ifdef NOTIFY_PTP_EVENT
        /* Report once when not supposed to receive this message. */
        if (p->report_announce) {
            p->report_announce = 0;
            exception_log(0, 0, "Announce %04x received at p-%u",
                          msg->hdr.sequenceId, p->id);
        }
#endif
        return;
    }
    if (msg->data.announce.stepsRemoved >= 255)
        return;

    /* Ports may be looping. */
    if (same_clock_id(&p->c->id, &msg->hdr.sourcePortIdentity.clockIdentity)) {
        if (msg->hdr.sourcePortIdentity.port < p->id) {
            if (p->state == PTP_S_PASSIVE)
                port_set_announce_timeout(p);
            else
                port_set_state(p, PTP_S_PASSIVE);
        }
        return;
    }
    if (check_path_trace(p->c, msg))
        return;

    if (p->gptp)
        ptp_forward(p, m);

    ptp_save_rx_msg(m);
}  /* handle_announce */

static void proc_announce(struct ptp_port *p, struct ptp_message *m)
{
    struct ptp_msg *msg = m->msg;
    struct bmc_dataset ds;
    int result = 0;

    announce_to_dataset(msg, &ds, p);
    switch (p->state) {
    case PTP_S_INITIALIZING:
    case PTP_S_FAULTY:
    case PTP_S_DISABLED:
        break;

    /* Maintain Announces in these states. */
    case PTP_S_PASSIVE:
    case PTP_S_UNCALIBRATED:
    case PTP_S_SLAVE:
        result = update_current_master(p, &ds);
        break;
    default:
        result = add_foreign_master(p, &ds);
        break;
    }
    if (result) {
        handle_state_decision(p->c);
    }
}  /* proc_announce */

static void port_tx_delay_resp(struct ptp_port *p, struct ptp_message *m)
{
    struct ptp_message *rsp;
    struct ptp_msg *msg;
    struct ptp_msg *req = m->msg;

    rsp = clock_new_msg(p->c, DELAY_RESP_MSG, NULL);
    msg = rsp->msg;

    msg->hdr.domainNumber = req->hdr.domainNumber;
    msg->hdr.sequenceId = req->hdr.sequenceId;
    if (!p->no_id_replace)
        msg->hdr.sourcePortIdentity.port = p->id;
    msg->hdr.correctionField = req->hdr.correctionField;
    ptp_utime_to_timestamp(0, &m->rx, &msg->data.delay_resp.receiveTimestamp);
    memcpy(&msg->data.delay_resp.requestingPortIdentity,
        &req->hdr.sourcePortIdentity, sizeof(struct ptp_port_identity));

    ptp_msg_tx(msg);
    ptp_tx_msg(rsp, p->index, 1 << (p->index - 1), 7, p->c->transport,
               p->c->vid, 0, 0, NULL);
}  /* port_tx_delay_resp */

static void handle_delay_req(struct ptp_port *p, struct ptp_message *m)
{
    if (p->ds.delayMechanism != DELAY_E2E)
        return;
    if (p->state != PTP_S_MASTER)
        return;

    ptp_save_rx_msg(m);
}  /* handle_delay_req */

static void proc_delay_req(struct ptp_port *p, struct ptp_message *m)
{
    port_tx_delay_resp(p, m);
}  /* proc_delay_req */

static void proc_delay_response(struct ptp_port *p)
{
    struct ptp_msg_info *req = &p->tx_delay_req;
    struct ptp_msg_info *rsp = &p->rx_delay_resp;

    port_path_delay(p, &req->hw_time, &rsp->ptp_time, rsp->correction);

    msg_info_clear(req);
    msg_info_clear(rsp);
}  /* proc_delay_response */

static void handle_delay_resp(struct ptp_port *p, struct ptp_message *m)
{
    struct ptp_msg *msg;
    struct ptp_msg_info *req = &p->tx_delay_req;

    /* Not sending Delay_Req. */
    if (!msg_info_valid(req))
        return;

    if (!accept_slave_msg(p, m, true))
        return;

    msg = m->msg;
    if (!same_port_id(&msg->data.delay_resp.requestingPortIdentity,
         &req->id))
         return;
    if (msg->hdr.sequenceId != req->seqid)
        return;

    if (p->ds.logMinDelayReqInterval != msg->hdr.logMessageInterval) {
        if (!msg->hdr.flagField.flag.unicastFlag &&
            msg->hdr.logMessageInterval > -4 &&
            msg->hdr.logMessageInterval < 8) {
            p->ds.logMinDelayReqInterval = msg->hdr.logMessageInterval;
        }
    }
    msg_info_save(&p->rx_delay_resp, msg, NULL);
    if (msg_info_ts_valid(req)) {
        ptp_save_rx_msg(m);
    }
}  /* handle_delay_resp */

static void proc_delay_resp(struct ptp_port *p, struct ptp_message *m)
{
    proc_delay_response(p);
}  /* proc_delay_resp */

static void handle_delayed_delay_req(struct ptp_port *p, struct ptp_message *m)
{
    struct ptp_msg_info *req = &p->tx_delay_req;
    struct ptp_msg_info *rsp = &p->rx_delay_resp;

    /* No Delay_Req sent.  Unlikely to happen. */
    if (!msg_info_valid(req))
        return;

    /* Save the transmit timestamp. */
    memcpy(&req->hw_time, &m->rx, sizeof(struct ptp_utime));

    /* Delay_Resp received. */
    if (msg_info_valid(rsp)) {
        if (rsp->seqid == req->seqid)
            ptp_save_rx_msg(m);
    }
}  /* handle_delayed_delay_req */

static void proc_delayed_delay_req(struct ptp_port *p, struct ptp_message *m)
{
    struct ptp_msg_info *req = &p->tx_delay_req;
    struct ptp_msg_info *rsp = &p->rx_delay_resp;

    /* Delay_Resp received. */
    if (msg_info_valid(rsp)) {
        if (rsp->seqid == req->seqid)
            proc_delay_response(p);
    }
}  /* proc_delayed_delay_req */

static void port_tx_pdelay_resp(struct ptp_port *p, struct ptp_message *m)
{
    struct ptp_message *rsp;
    struct ptp_msg *msg;
    struct ptp_msg *req = m->msg;
    u8 *addr = NULL;
    uint sec;
    uint nsec;

    rsp = clock_new_msg(p->c, PDELAY_RESP_MSG, NULL);
    msg = rsp->msg;

    msg->hdr.domainNumber = req->hdr.domainNumber;
    msg->hdr.sequenceId = req->hdr.sequenceId;
    msg->hdr.sourcePortIdentity.port = p->id;
    msg->hdr.correctionField = req->hdr.correctionField;
    if (p->p2p_two_step) {
        sec = 0;
        nsec = 0;
        msg->hdr.flagField.flag.twoStepFlag = 1;
        ptp_utime_to_timestamp(0, &m->rx,
            &msg->data.pdelay_resp.requestReceiptTimestamp);
    } else {
        sec = m->rx.sec;
        nsec = m->rx.nsec;
    }
    if (p->peer_index && p->peer_addr[0] != 0xFF) {
        msg->hdr.flagField.flag.unicastFlag = 1;
        addr = p->peer_addr;
    }
    memcpy(&msg->data.pdelay_resp.requestingPortIdentity,
        &req->hdr.sourcePortIdentity, sizeof(struct ptp_port_identity));

    /* Save necessary information to handle transmit timestamp. */
    msg_info_save(&p->tx_pdelay_resp, msg, NULL);

    ptp_msg_tx(msg);
    ptp_tx_msg(rsp, p->index, 1 << (p->index - 1), 7, p->c->transport,
               p->c->vid, sec, nsec, addr);
}  /* port_tx_pdelay_resp */

static void handle_delayed_pdelay_resp(struct ptp_port *p, struct ptp_message *m)
{
    struct ptp_msg_info *rsp = &p->tx_pdelay_resp;
    struct ptp_message *fup;
    struct ptp_msg *msg;

    /* Using 1-step. */
    if (!msg_info_valid(rsp))
        return;
    if (!rsp->two_step)
        return;

    if (rsp->seqid != m->msg->hdr.sequenceId)
        return;

    ptp_save_rx_msg(m);
}  /* handle_delayed_pdelay_resp */

static void proc_delayed_pdelay_resp(struct ptp_port *p, struct ptp_message *m)
{
    struct ptp_msg_info *rsp = &p->tx_pdelay_resp;
    struct ptp_message *fup;
    struct ptp_msg *msg;
    u8 *addr = NULL;

    tx_msg_lock();
    fup = clock_new_msg(p->c, PDELAY_RESP_FOLLOW_UP_MSG, NULL);
    msg = fup->msg;

    msg->hdr.domainNumber = rsp->domain;
    msg->hdr.sequenceId = rsp->seqid;
    msg->hdr.sourcePortIdentity.port = p->id;
    memcpy(&msg->data.pdelay_resp.requestingPortIdentity,
        &rsp->id, sizeof(struct ptp_port_identity));
    if (p->peer_index && p->peer_addr[0] != 0xFF) {
        msg->hdr.flagField.flag.unicastFlag = 1;
        addr = p->peer_addr;
    }

    ptp_utime_to_timestamp(0, &m->rx,
        &msg->data.pdelay_resp_follow_up.responseOriginTimestamp);

    msg_info_clear(rsp);

    ptp_msg_tx(msg);
    ptp_tx_msg(fup, p->index, 1 << (p->index - 1), 7, p->c->transport,
               p->c->vid, 0, 0, addr);
    tx_msg_unlock();
}  /* proc_delayed_pdelay_resp */

static void handle_pdelay_req(struct ptp_port *p, struct ptp_message *m)
{
    struct ptp_msg *req = m->msg;

    if (p->ds.delayMechanism != DELAY_P2P)
        return;

    /* TimeReceiver does not respond to Pdelay_Req. */
    if (p->use_one_way_resp &&
        (p->state == PTP_S_SLAVE || p->state == PTP_S_UNCALIBRATED))
        return;

    check_peer_info(p, req, &m->payload[6]);

    ptp_save_rx_msg(m);
}  /* handle_pdelay_req */

static void proc_pdelay_req(struct ptp_port *p, struct ptp_message *m)
{
    struct ptp_msg *req = m->msg;

    if (p->resp_missing && !p->cnt_delay_resp) {
        p->cnt_delay_resp = 5;
    }

    /* Remember peer port identity. */
    if (!p->as_capable_set) {
        if (p->peer_valid && !p->no_id_check) {
            if (!same_port_id(&req->hdr.sourcePortIdentity, &p->peer_id)) {
                p->peer_valid = 0;
            }
        } else if (!p->peer_valid) {
            p->peer_valid = 1;
            memcpy(&p->peer_id, &req->hdr.sourcePortIdentity,
                sizeof(struct ptp_port_identity));
        }
    }
    tx_msg_lock();
    port_tx_pdelay_resp(p, m);
    tx_msg_unlock();
}  /* proc_pdelay_req */

static void proc_each_pdelay_response(struct ptp_port *p, u8 n)
{
    struct ptp_msg_info *req = &p->tx_delay_req;
    struct ptp_msg_info *rsp = &p->rx_delay_resp;
    struct ptp_msg_info *fup = &p->rx_pdelay_fup;
    struct ptp_pdelay_info *info = &p->pdelay_info;
    struct ptp_timestamp *fup_timestamp = NULL;
    s64 fup_corr = 0;

    if (p->multi_pdelay) {
        rsp = &p->multi_pdelay->rx_pdelay_resp[n];
        fup = &p->multi_pdelay->rx_pdelay_fup[n];
        info = &p->multi_pdelay->pdelay_info[n];
    }
    if (rsp->two_step) {
        fup_timestamp = &fup->ptp_time;
        fup_corr = fup->correction;
    }
    port_peer_delay(p, info, &req->hw_time, &rsp->hw_time, &rsp->ptp_time,
        rsp->correction, fup_timestamp, fup_corr);

    /* Indicate peer delay is calculated. */
    rsp->resp = 1;
    fup->resp = 1;
}  /* proc_each_pdelay_response */

static void proc_pdelay_response(struct ptp_port *p, u8 n)
{
#ifdef NOTIFY_PTP_EVENT
    struct ptp_pdelay_info *info = &p->pdelay_info;
#endif
    struct ptp_msg_info *req = &p->tx_delay_req;
    struct ptp_msg_info *rsp = &p->rx_delay_resp;
    struct ptp_msg_info *fup = &p->rx_pdelay_fup;
    u8 i = 0;

    /* Only process one response */
    if (n < 8) {
        i = n;
        ++n;
    }
    if (p->multi_pdelay) {
        rsp = &p->multi_pdelay->rx_pdelay_resp[i];
        fup = &p->multi_pdelay->rx_pdelay_fup[i];
#ifdef NOTIFY_PTP_EVENT
        info = &p->multi_pdelay->pdelay_info[i];
#endif
    }
    for (; i < n; i++) {
        if ((rsp->valid && !rsp->resp) &&
            (!rsp->two_step ||
            (fup->valid && !fup->resp)))
            proc_each_pdelay_response(p, i);
        ++rsp;
        ++fup;
    }
    if (!req->resp && p->cnt_delay_resp > 0)
        p->cnt_delay_resp--;

    /* Have at least one response. */
    req->resp = 1;

#ifdef NOTIFY_PTP_EVENT
if (p->resp_missing)
dbg_msg("missing: %d\n", p->resp_missing);
    if (p->report_resp)
        exception_log(0, 0,
            "Pdelay_Resp %04x received at p-%u with delay %d",
            req->seqid, p->index, (int) info->delay);
#endif

    /* Clear missing response counter. */
    p->resp_missing = 0;
    p->report_resp = 0;
    p->peer_resp = 1;
    port_check_capable(p);
}  /* proc_pdelay_response */

static void handle_pdelay_resp(struct ptp_port *p, struct ptp_message *m)
{
    struct ptp_msg_info *req = &p->tx_delay_req;
    struct ptp_msg_info *rsp = &p->rx_delay_resp;
    struct ptp_msg *msg;
    u8 n;

    msg = m->msg;

    /* Not sending Pdelay_Req. */
    if (!msg_info_valid(req))
        return;

    if (!same_port_id(&msg->data.pdelay_resp.requestingPortIdentity,
        &req->id))
        return;
    if (msg->hdr.sequenceId != req->seqid)
        return;

    if (p->multi_pdelay) {
        n = find_msg_info(p->multi_pdelay->rx_pdelay_resp, MAX_PDELAY_RESP,
                          &msg->hdr.sourcePortIdentity);
        if (n == MAX_PDELAY_RESP)
            return;
        rsp = &p->multi_pdelay->rx_pdelay_resp[n];
    }

    p->resp_cnt++;
    if (p->resp_cnt > 1) {
        if (!p->allow_multi_resp)
            p->mult_resp = 1;
        port_check_capable(p);
        if (p->mult_resp) {
            p->mult_resp_cnt++;
            if (p->mult_resp_cnt >= allowed_pdelay_faults) {
                p->mult_resp_cnt = 0;
                p->mult_resp_fup_cnt = 0;
                port_set_fault_timeout(p, 1, MULT_RESP_FAULT_TIMEOUT);
                return;
            }
        }
    }

    /* Save Pdelay_Resp receive information. */
    msg_info_save(rsp, msg, &m->rx);

    /* No Pdelay_Req transmit timestamp yet. */
    if (!msg_info_ts_valid(req)) {
        return;
    }

    /* Process Pdelay_Resp if one-step. */
    if (!msg->hdr.flagField.flag.twoStepFlag) {
        ptp_save_rx_msg(m);
    }
}  /* handle_pdelay_resp */

static void proc_pdelay_resp(struct ptp_port *p, struct ptp_message *m)
{
    struct ptp_msg_info *req = &p->tx_delay_req;
    struct ptp_msg *msg;

    msg = m->msg;

    /* No Pdelay_Req transmit timestamp yet. */
    if (!msg_info_ts_valid(req)) {
        return;
    }

    /* Process Pdelay_Resp if one-step. */
    if (!msg->hdr.flagField.flag.twoStepFlag) {
        u8 n = 0;

        if (p->multi_pdelay)
            n = find_msg_info(p->multi_pdelay->rx_pdelay_resp, MAX_PDELAY_RESP,
                              &msg->hdr.sourcePortIdentity);

        proc_pdelay_response(p, n);
    }
}  /* proc_pdelay_resp */

static void handle_pdelay_resp_fup(struct ptp_port *p, struct ptp_message *m)
{
    struct ptp_msg_info *req = &p->tx_delay_req;
    struct ptp_msg_info *rsp = &p->rx_delay_resp;
    struct ptp_msg_info *fup = &p->rx_pdelay_fup;
    struct ptp_msg *msg;

    msg = m->msg;

    /* Not sending Pdelay_Req. */
    if (!msg_info_valid(req))
        return;

    if (!same_port_id(&msg->data.pdelay_resp.requestingPortIdentity,
        &req->id))
        return;
    if (msg->hdr.sequenceId != req->seqid)
        return;

    if (p->multi_pdelay) {
        u8 n;

        n = find_msg_info(p->multi_pdelay->rx_pdelay_resp, MAX_PDELAY_RESP,
                          &msg->hdr.sourcePortIdentity);
        if (n == MAX_PDELAY_RESP)
            return;
        rsp = &p->multi_pdelay->rx_pdelay_resp[n];
        fup = &p->multi_pdelay->rx_pdelay_fup[n];
    }

    /* sourcePortIdentity has to be the same for both responses. */
    if (!same_port_id(&msg->hdr.sourcePortIdentity, &rsp->id))
        return;

    p->resp_fup_cnt++;
    if (p->resp_fup_cnt > 1) {
        if (!p->allow_multi_resp)
            p->mult_resp = 1;
        port_check_capable(p);
        if (p->mult_resp) {
            p->mult_resp_fup_cnt++;
            if (p->mult_resp_fup_cnt >= 3) {
                p->mult_resp_cnt = 0;
                p->mult_resp_fup_cnt = 0;
                port_set_fault_timeout(p, 1, MULT_RESP_FAULT_TIMEOUT);
                return;
            }
        }
    } else if (p->mult_resp_fup_cnt) {
        /* Gradually reduce the multiple responses count. */
        p->mult_resp_fup_cnt--;
    }

    /* Save Pdelay_Resp_Follow_Up receive information. */
    msg_info_save(fup, msg, NULL);

    /* No Pdelay_Req transmit timestamp yet. */
    if (!msg_info_ts_valid(req)) {
        return;
    }

    /* Pdelay_Resp is received.  It is unlikely Pdelay_Resp_Follow_Up is
     * received out of order.
     */
    if (msg_info_valid(rsp)) {
        ptp_save_rx_msg(m);
    }
}  /* handle_pdelay_resp_fup */

static void proc_pdelay_resp_fup(struct ptp_port *p, struct ptp_message *m)
{
    struct ptp_msg_info *req = &p->tx_delay_req;
    struct ptp_msg_info *rsp = &p->rx_delay_resp;
    u8 n = 0;

    /* No Pdelay_Req transmit timestamp yet. */
    if (!msg_info_ts_valid(req)) {
        return;
    }
    if (p->multi_pdelay) {
        n = find_msg_info(p->multi_pdelay->rx_pdelay_fup, MAX_PDELAY_RESP,
                          &m->msg->hdr.sourcePortIdentity);
        rsp = &p->multi_pdelay->rx_pdelay_resp[n];
    }

    /* Pdelay_Resp is received.  It is unlikely Pdelay_Resp_Follow_Up is
     * received out of order.
     */
    if (msg_info_valid(rsp))
        proc_pdelay_response(p, n);
}  /* proc_pdelay_resp_fup */

static void handle_delayed_pdelay_req(struct ptp_port *p, struct ptp_message *m)
{
    struct ptp_msg_info *req = &p->tx_delay_req;
    struct ptp_msg_info *rsp = &p->rx_delay_resp;
    struct ptp_msg_info *fup = &p->rx_pdelay_fup;

    /* No Pdelay_Req sent.  Unlikely to happen. */
    if (!msg_info_valid(req))
        return;

    /* Save the transmit timestamp. */
    memcpy(&req->hw_time, &m->rx, sizeof(struct ptp_utime));
    if (p->multi_pdelay) {
        rsp = &p->multi_pdelay->rx_pdelay_resp[0];
        fup = &p->multi_pdelay->rx_pdelay_fup[0];
    }

    /* Pdelay_Resp received. */
    if (msg_info_valid(rsp)) {
        if (rsp->seqid != req->seqid)
            return;

        /* Process Pdelay_Resp if one-step or Pdelay_Resp_Follow_Up is
         * received.
        */
        if (!rsp->two_step ||
            msg_info_valid(fup)) {
            ptp_save_rx_msg(m);
        }
    }
}  /* handle_delayed_pdelay_req */

static void proc_delayed_pdelay_req(struct ptp_port *p, struct ptp_message *m)
{
    struct ptp_msg_info *req = &p->tx_delay_req;
    struct ptp_msg_info *rsp = &p->rx_delay_resp;
    struct ptp_msg_info *fup = &p->rx_pdelay_fup;
    u8 n = 0;

    if (p->multi_pdelay) {
        rsp = &p->multi_pdelay->rx_pdelay_resp[0];
        fup = &p->multi_pdelay->rx_pdelay_fup[0];
        n = MAX_PDELAY_RESP;
    }

    /* Pdelay_Resp received. */
    if (msg_info_valid(rsp)) {
        if (rsp->seqid != req->seqid)
            return;

        /* Process Pdelay_Resp if one-step or Pdelay_Resp_Follow_Up is
         * received.
        */
        if (!rsp->two_step ||
            msg_info_valid(fup)) {
            proc_pdelay_response(p, n);
        }
    }
}  /* proc_delayed_pdelay_req */

/* Workaround for test case. */
static u8 spec;

static void ptp_rx(struct ptp_port *p, struct ptp_msg *msg,
    struct ptp_utime *rx, bool delayed, u8 *src)
{
    struct ptp_message *m;

    /* Check transportSpecific for gPTP. */
    if (p->gptp && msg->hdr.transportSpecific != 1) {
        if (spec != msg->hdr.transportSpecific)
            p->resp_missing = ALLOWED_LOST_RESPONSES - 1;
        spec = msg->hdr.transportSpecific;
        return;
    }

    /* Only process message when at least at LISTENING state. */
    if (p->state <= PTP_S_DISABLED)
        return;

    if (!p->c ||
        msg->hdr.domainNumber != p->c->domain ||
        msg->hdr.transportSpecific != p->c->specific)
        return;

    m = alloc_msg(msg, ntohs(msg->hdr.messageLength), rx);
    if (!m)
        return;
    if (src)
        memcpy(&m->payload[6], src, 6);

    m->p = p;
    m->save.delayed = delayed;
    switch (msg->hdr.messageType) {
    case SYNC_MSG:
        if (delayed)
            handle_delayed_sync(p, m);
        else
            handle_sync(p, m);
        break;
    case FOLLOW_UP_MSG:
        handle_fup(p, m);
        break;
    case ANNOUNCE_MSG:
        handle_announce(p, m);
        break;
    case DELAY_REQ_MSG:
        if (delayed)
            handle_delayed_delay_req(p, m);
        else
            handle_delay_req(p, m);
        break;
    case DELAY_RESP_MSG:
        handle_delay_resp(p, m);
        break;
    case PDELAY_REQ_MSG:
        if (delayed)
            handle_delayed_pdelay_req(p, m);
        else
            handle_pdelay_req(p, m);
        break;
    case PDELAY_RESP_MSG:
        if (delayed)
            handle_delayed_pdelay_resp(p, m);
        else
            handle_pdelay_resp(p, m);
        break;
    case PDELAY_RESP_FOLLOW_UP_MSG:
        handle_pdelay_resp_fup(p, m);
        break;
    case MANAGEMENT_MSG:
        handle_management(p, m);
        break;
    case SIGNALING_MSG:
        handle_signaling(p, m);
        break;
    default:
        break;
    }
    msg_put(m);
}  /* ptp_rx */

static void ptp_setup_rx(struct ptp_msg *msg, u8 port, u32 sec, u32 nsec,
    bool delayed, u8 *src, bool raw, u16 vid)
{
    struct ptp_utime rx;
    struct ptp_utime *ptr = NULL;
    struct ptp_port *p;

    p = get_ptp_port(port);
    if (!p)
        return;

    /* Send the Ethernet Ready status now as link is connected. */
    if (p->c->report_link && p->report_ready) {
        schedule_work(&p->c->ethernet_ready_work);
    }

    /* Match Vlan ID. */
    if (!p->c || (!delayed &&
        ((vid && vid != (p->c->vid & 0xfff)) ||
        (raw != (p->c->transport == 0)))))
        return;
    if (nsec) {
        rx.sec = sec;
        rx.nsec = nsec;
        ptr = &rx;
    }

    ptp_rx(p, msg, ptr, delayed, src);
}

static void ptp_proc_rx(struct ptp_message *m)
{
    struct ptp_msg *msg = m->msg;
    struct ptp_port *p = m->p;

    switch (msg->hdr.messageType) {
    case SYNC_MSG:
        if (m->save.delayed)
            proc_delayed_sync(p, m);
        else
            proc_sync(p, m);
        break;
    case FOLLOW_UP_MSG:
        proc_fup(p, m);
        break;
    case ANNOUNCE_MSG:
        proc_announce(p, m);
        break;
    case DELAY_REQ_MSG:
        if (m->save.delayed)
            proc_delayed_delay_req(p, m);
        else
            proc_delay_req(p, m);
        break;
    case DELAY_RESP_MSG:
        proc_delay_resp(p, m);
        break;
    case PDELAY_REQ_MSG:
        if (m->save.delayed)
            proc_delayed_pdelay_req(p, m);
        else
            proc_pdelay_req(p, m);
        break;
    case PDELAY_RESP_MSG:
        if (m->save.delayed)
            proc_delayed_pdelay_resp(p, m);
        else
            proc_pdelay_resp(p, m);
        break;
    case PDELAY_RESP_FOLLOW_UP_MSG:
        proc_pdelay_resp_fup(p, m);
        break;
    case MANAGEMENT_MSG:
        proc_management(p, m);
        break;
    case SIGNALING_MSG:
        proc_signaling(p, m);
        break;
    default:
        break;
    }
}
