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

#include "ptp_clock.h"
#include "ptp_config.h"
#include "ptp_forward.h"
#include "ptp_protocol.h"


static struct ptp_clock ptp_clocks[MAX_PTP_CLOCKS];

struct ptp_clock *get_ptp_clock(u8 domain)
{
    int i;

    for (i = 0; i < MAX_PTP_CLOCKS; i++) {
        if (ptp_clocks[i].domain == domain)
            return &ptp_clocks[i];
    }
    return NULL;
}

struct ptp_message *clock_get_msg(struct ptp_clock *c, struct ptp_msg *msg,
    struct ptp_message *m)
{
    if (!m)
        m = get_tx_msg();
    memcpy(m->msg, msg, msg->hdr.messageLength);
    return m;
}

struct ptp_message *clock_new_msg(struct ptp_clock *c, u8 type,
    struct ptp_message *m)
{
    struct ptp_msg *msg;
    struct ptp_msg *temp;

    if (!m)
        m = get_tx_msg();
    msg = m->msg;
    switch (type) {
    case ANNOUNCE_MSG:
        temp = &c->ann_template;
        break;
    case SYNC_MSG:
        temp = &c->sync_template;
        break;
    case FOLLOW_UP_MSG:
        temp = &c->fup_template;
        break;
    case DELAY_REQ_MSG:
        temp = &c->delay_req_template;
        break;
    case DELAY_RESP_MSG:
        temp = &c->delay_resp_template;
        break;
    case PDELAY_REQ_MSG:
    case PDELAY_RESP_MSG:
    case PDELAY_RESP_FOLLOW_UP_MSG:
        temp = &c->pdelay_req_template;
        break;
    case MANAGEMENT_MSG:
        temp = &c->manage_template;
        break;
    case SIGNALING_MSG:
        temp = &c->signal_template;
        break;
    default:
        return NULL;
    }
    memcpy(msg, temp, temp->hdr.messageLength);
    switch (type) {
    case PDELAY_RESP_MSG:
    case PDELAY_RESP_FOLLOW_UP_MSG:
        msg->hdr.messageType = type;
        break;
    default:
        break;
    }
    return m;
}  /* clock_new_msg */

void clock_tx_announce(struct ptp_clock *c)
{
    struct ptp_port *h = &c->host_port;
    struct ptp_port *p;
    int n;

    if (c->no_announce)
        return;

    /* Regular 1588 PTP profile. */
    if (!h->gptp) {
        tx_msg_lock();
        port_tx_announce(h);
        tx_msg_unlock();
    } else if (h->state == PTP_S_MASTER) {
        for (n = 1; n <= c->cnt; n++) {
            p = c->ptp_ports[n];
            if (p->state != PTP_S_MASTER)
                continue;
            tx_msg_lock();
            port_tx_announce(p);
            tx_msg_unlock();
        }
    } else if (!c->no_announce) {
#ifdef DBG_PTP_FWD
dbg_msg(" fwd ann"NL);
#endif
        c->last_ann->msg->hdr.sequenceId++;
        for (n = 1; n <= c->cnt; n++) {
            p = c->ptp_ports[n];
            if (p->state != PTP_S_MASTER)
                continue;

            /* Forward only when asCapable. */
            if (!port_capable(p))
                continue;
            tx_msg_lock();
            port_fwd_announce(p, c->last_ann);
            tx_msg_unlock();
        }
    }
}  /* clock_tx_announce */

void clock_tx_sync(struct ptp_clock *c)
{
    struct ptp_port *h = &c->host_port;
    struct ptp_port *p;
    int n;

    /* Regular 1588 PTP profile. */
    if (!h->gptp) {
        tx_msg_lock();
        port_tx_sync(h);
        tx_msg_unlock();
    } else if (h->state == PTP_S_MASTER) {
#ifdef DBG_PTP_TIMEOUT
dbg_sync_timeout = 0;
#endif
        for (n = 1; n <= c->cnt; n++) {
            p = c->ptp_ports[n];
            if (p->state != PTP_S_MASTER)
                continue;
            tx_msg_lock();
            port_tx_sync(p);
            tx_msg_unlock();
        }

    /* Make sure last_sync and last_fup are available when sending Sync from
     * clock_interval_work().
     */
    } else if (c->last_sync && c->last_fup) {
        c->first_sync_tx.sec = 0;
        ptp_check_residence(c);
        for (n = 1; n <= c->cnt; n++) {
            p = c->ptp_ports[n];
            if (p->state != PTP_S_MASTER)
                continue;

            /* Forward only when asCapable. */
            if (!port_capable(p))
                continue;
            tx_msg_lock();
            c->tx_sync_seqid = c->last_sync->msg->hdr.sequenceId;
            c->tx_fup_seqid = c->last_sync->msg->hdr.sequenceId;
            port_fwd_sync(p, c->last_sync);
            tx_msg_unlock();
        }
    }
}  /* clock_tx_sync */

static void clock_sync_work(struct ksz_schedule_work *work)
{
    struct ptp_clock *c = work->dev;

    /* No active port. */
    if (!c->live_ports)
        return;
    c->ann_cnt++;
    c->ann_cnt &= c->ann_max;
    if (!c->ann_cnt)
        clock_tx_announce(c);
    c->sync_cnt++;
    c->sync_cnt &= c->sync_max;
    if (!c->sync_cnt)
        clock_tx_sync(c);

    /* For debug purpose. */
    c->time_cnt++;
    if (!(c->time_cnt % 100)) {
        struct ptp_port *p;
        int i;

        for (i = 1; i <= c->cnt; i++) {
            p = c->ptp_ports[i];
            if (p->p2p && !p->cnt_delay_resp)
                p->cnt_delay_resp = 10;
        }
    }
}

static void clock_sync_func(TimerHandle_t xTimer)
{
    struct ksz_timer_info *timer = (struct ksz_timer_info *)
        pcTimerGetName(xTimer);
    struct ptp_clock *c = timer->param;

    /* Last period is shorter than normal. */
#ifdef DBG_MSG
    if (c->last_sync_time_jiffies == 1)
dbg_msg("last_sync short\n");
#endif
    if (c->last_sync_time_jiffies == 1)
        ksz_change_timer(&c->sync_timer_info, c->sync_timer_info.period,
            c->sync_timer_info.period);
    schedule_work(&c->sync_work);
    time_save(&c->last_sync_time_jiffies, 2);
}

static void clock_sync_timeout(struct ptp_clock *c)
{
    struct ptp_message *sync;
    struct ptp_message *fup;
    struct ptp_msg *temp;
    struct ptp_msg *msg;
#ifdef NOTIFY_PTP_EVENT
    void *fd = dev[1].fd;
    char log[240], rcv[80];
    uint sec = 0;
    uint nsec = 0;
    int n, rc;

    rc = get_clock(fd, &sec, &nsec, c->index);
    (void) rc;
    timestamp_str(sec, nsec, rcv, sizeof(rcv));
    n = snprintf(log, sizeof(log), "Sync lost at %s", rcv);
    if (c->last_sync_rx.sec) {
        if (c->last_rx_fup) {
            char fup[80];

            if (n >= sizeof(log))
                n = sizeof(log);
            timestamp_str(c->last_sync_rx.sec, c->last_sync_rx.nsec, rcv,
                sizeof(rcv));
            timestamp_str(
                c->last_rx_fup->msg->data.follow_up.preciseOriginTimestamp.sec.lo,
                c->last_rx_fup->msg->data.follow_up.preciseOriginTimestamp.nsec,
                fup, sizeof(fup));
#if 0
            n += snprintf(&log[n], sizeof(log) - n,
                NL" Last Follow_Up received by %u:%9u with timestamp %u:%9u",
                c->last_sync_rx.sec, c->last_sync_rx.nsec,
                c->last_rx_fup->msg->data.follow_up.preciseOriginTimestamp.sec.lo,
                c->last_rx_fup->msg->data.follow_up.preciseOriginTimestamp.nsec);
#endif
            n += snprintf(&log[n], sizeof(log) - n,
                NL" Last Follow_Up received by %s "
                NL"  with timestamp %s", rcv, fup);
        }
    }
    exception_log(sec, nsec, log);
#endif

    /* Reset to report receiving Sync again. */
    c->first_sync = false;
    c->report_sync = true;
    c->prev_sync_jiffies = 0;
#if 0
    do {
        struct ptp_port *p = clock_slave_port(c);

        if (p && !p->no_link)
            p->report_ready_done = 1;
    } while (0);
#endif

    /* Initialize to invalid values.  Set to different values so that only
     * Follow_Up received after Sync can exit timeout state.
     */
    c->rx_sync_seqid = 0x20000;
    c->rx_fup_seqid = 0x20001;
    if (c->last_sync_rx.sec) {

        /* Align sequenceId of last_sync and last_rx_fup. */
        c->last_rx_fup->msg->hdr.sequenceId =
            c->last_sync->msg->hdr.sequenceId;
        msg_save(&c->last_fup, c->last_rx_fup);
        return;
    }

    /* No need to forward when not acting as a master clock. */
    if (clock_class(c) == 255)
        return;

    /* In case these messages are allocated again. */
    msg_clear(&c->last_sync);
    msg_clear(&c->last_fup);
    msg_clear(&c->last_rx_fup);
    msg_clear(&c->base_sync);
    msg_clear(&c->base_fup);

    temp = &c->sync_template;
    sync = alloc_msg(NULL, temp->hdr.messageLength, NULL);
    if (!sync)
        return;
    msg = sync->msg;
    memcpy(msg, temp, temp->hdr.messageLength);
    if (!c->no_announce)
        memcpy(&msg->hdr.sourcePortIdentity.clockIdentity,
               &c->best_id, sizeof(struct ptp_clock_identity));

    msg->hdr.sequenceId = 0xffff;
    msg->hdr.flagField.flag.twoStepFlag = 1;
    c->last_sync = sync;
    msg_save(&c->base_sync, sync);

    temp = &c->fup_template;
    fup = alloc_msg(NULL, temp->hdr.messageLength +
        sizeof(struct ptp_organization_ext_tlv) +
        sizeof(struct IEEE_802_1AS_data_1) - 1, NULL);
    if (!fup)
        return;
    msg = fup->msg;
    memcpy(msg, temp, temp->hdr.messageLength);
    if (!c->no_announce)
        memcpy(&msg->hdr.sourcePortIdentity.clockIdentity,
               &c->best_id, sizeof(struct ptp_clock_identity));

    msg->hdr.sequenceId = 0xffff;
    clock_add_follow_up(c, msg);
    c->last_fup = fup;
    msg_save(&c->last_rx_fup, fup);
    msg_save(&c->base_fup, fup);
}  /* clock_sync_timeout */

static void clock_fup_timeout(struct ptp_clock *c);

static void clock_sync_timeout_work(struct ksz_schedule_work *work)
{
    struct ptp_clock *c = work->dev;
    struct ptp_port *h = &c->host_port;
    uint diff;
    int fup_timeout;

    /* Do not go into timeout state if Sync is received within 2 ms. */
    diff = time_delta(c->set_sync_timeout_jiffies, jiffies);
    if (diff <= 1) {

        /* If Follow_Up is then not received Sync timeout happens. */
        c->sync_timeout_ready = 1;
        return;
    }

    /* Sync interval is now handled by sync_timer. */
    ksz_stop_timer(&c->interval_timer_info);

    /* Check whether the Follow_Up timeout timer is running. */
    fup_timeout = (c->fup_timeout_info.max != 0);
    ksz_stop_timer(&c->fup_timeout_info);
    if (c->signaling)
        clock_stop_signaling_timer(c);
    mutex_lock(&rx_lock);

    /* Indicate need to check whether Sync is forwarded during timeout. */
    c->miss_sync = 1;

    /* Reset first so that it can be set later for Follow_Up timeout. */
    c->miss_fup = 0;

    /* Forward Follow_Up during Follow_Up timeout. */
    if (fup_timeout)
        clock_fup_timeout(c);
    clock_sync_timeout(c);
    c->sync_timeout = 1;
    mutex_unlock(&rx_lock);

    /* No valid saved messages to forward them continually. */
    if (!c->last_sync || !c->last_fup)
        return;
    clock_start_sync_timer(c);
    clock_change_sync_timer(c, h->logSyncInterval);

    /* Do not send immediately when the difference is small. */
    diff = time_delta(c->last_sync_jiffies, jiffies);
    if (diff >= ptp_interval_to_ms(h->logSyncInterval))
        schedule_work(&c->sync_work);
}

static void clock_sync_timeout_func(TimerHandle_t xTimer)
{
    struct ksz_timer_info *timer = (struct ksz_timer_info *)
        pcTimerGetName(xTimer);
    struct ptp_clock *c = timer->param;
    struct ptp_port *h = clock_host_port(c);

    /* Catch when the port is shutting off. */
    if (h->state == PTP_S_DISABLED)
        return;
    schedule_delayed_work(&c->sync_timeout_work, 0);
}

static void clock_fup_timeout(struct ptp_clock *c)
{
    struct ptp_message *sync;
    struct ptp_message *fup;
    struct ptp_msg *temp;
    struct ptp_msg *msg;
    struct ptp_port *p;
    int n;

#ifdef DBG_PTP_TIMEOUT
dbg_msg("fup timeout: %u"NL, c->fup_timeout_info.period);
#endif
    /* last_fup should be empty after receiving Sync but not Follow_Up. */
    if (c->last_fup)
        return;

    if (!c->last_rx_fup) {
        temp = &c->fup_template;
        fup = alloc_msg(NULL, temp->hdr.messageLength +
            sizeof(struct ptp_organization_ext_tlv) +
            sizeof(struct IEEE_802_1AS_data_1) - 1, NULL);
        if (!fup)
             return;
        msg = fup->msg;
        memcpy(msg, temp, temp->hdr.messageLength);

        memcpy(&msg->hdr.sourcePortIdentity,
            &c->last_sync->msg->hdr.sourcePortIdentity,
            sizeof(msg->hdr.sourcePortIdentity));
        clock_add_follow_up(c, msg);
        c->last_rx_fup = fup;
    }

    /* Align sequenceId as last_rx_fup is used to forward lost Follow_Up. */
    c->last_rx_fup->msg->hdr.sequenceId = c->last_sync->msg->hdr.sequenceId;
    msg_save(&c->last_fup, c->last_rx_fup);
    msg_save(&c->base_fup, c->last_rx_fup);
    c->miss_fup = 1;
    c->tx_fup_seqid = c->last_fup->msg->hdr.sequenceId;
    c->first_sync_tx.sec = 0;
    for (n = 1; n <= c->cnt; n++) {
        p = c->ptp_ports[n];
        if (p->state != PTP_S_MASTER)
            continue;

        /* Forward only when asCapable. */
        if (!port_capable(p))
            continue;
        tx_msg_lock();

        /* Sync timestamp is available. */
        if (p->tx_sync.hw_time.sec)
            ptp_fwd_fup(p, c->last_fup);
        tx_msg_unlock();
    }
}  /* clock_fup_timeout */

static void clock_fup_timeout_work(struct ksz_schedule_work *work)
{
    struct ptp_clock *c = work->dev;
    struct ptp_port *h = &c->host_port;

    mutex_lock(&rx_lock);
    clock_fup_timeout(c);

    /* When Follow_Up timeout happens after Sync timeout is supposed to happen
     * then it shall happen.
     */
    if (c->sync_timeout_ready) {
        schedule_delayed_work(&c->sync_timeout_work, 0);
    }
    mutex_unlock(&rx_lock);
}

static void clock_fup_timeout_func(TimerHandle_t xTimer)
{
    struct ksz_timer_info *timer = (struct ksz_timer_info *)
        pcTimerGetName(xTimer);
    struct ptp_clock *c = timer->param;
    struct ptp_port *h = clock_host_port(c);

    /* One-shot timer. */
    if (!timer->repeat)
        timer->max = 0;

    /* Catch when the port is shutting off. */
    if (h->state == PTP_S_DISABLED)
        return;
    schedule_delayed_work(&c->fup_timeout_work, 0);
}

#define SYNC_MISS_MARGIN  4

void clock_set_sync_interval(struct ptp_clock *c)
{
    struct ptp_port *h = &c->host_port;
    int delay = ptp_interval_to_ms(h->logSyncInterval);
    uint ticks;

    /* Regular sync timer is running. */
    if (c->sync_timer_info.max)
        return;

    /* Start sync interval timer to be ready to send Sync. */
    ticks = msecs_to_jiffies(delay);
    c->interval_timer_info.period = ticks;
    ticks += SYNC_MISS_MARGIN;
    if (!c->interval_timer_info.max)
        ksz_start_timer(&c->interval_timer_info,
            c->interval_timer_info.period);
    if (c->interval_timer_info.repeat)
        ksz_change_timer(&c->interval_timer_info, ticks,
            c->interval_timer_info.period);
}

void clock_stop_sync_interval(struct ptp_clock *c)
{
    ksz_stop_timer(&c->interval_timer_info);
}

static void clock_interval_work(struct ksz_schedule_work *work)
{
    struct ptp_clock *c = work->dev;
    bool skip = true;
    uint diff;

    mutex_lock(&rx_lock);
    diff = time_delta(c->last_sync_jiffies, jiffies);

    /* Cannot send Sync/Follow_Up pair if these messages are not available. */
    if (!c->last_sync || !c->last_fup)
        diff = 0;

    /* This routine is reached before the Sync message is received. */
    if (diff >= c->interval_timer_info.period) {

        /* Indicate Sync is sent so skip forwarding next received Sync. */
        c->miss_sync = 1;
        c->miss_fup = 1;
        skip = false;
    }
    mutex_unlock(&rx_lock);

    /* Sometimes the difference is 0, 1, or 2. */
    if (skip)
        return;

    /* Start forwarding last Sync. */
    c->ann_cnt = 0;
    c->sync_cnt = c->sync_max;
    schedule_work(&c->sync_work);
}

static void clock_interval_func(TimerHandle_t xTimer)
{
    struct ksz_timer_info *timer = (struct ksz_timer_info *)
        pcTimerGetName(xTimer);
    struct ptp_clock *c = timer->param;

    if (c->interval_timer_info.repeat)
        ksz_change_timer(&c->interval_timer_info,
            c->interval_timer_info.period,
            c->interval_timer_info.period);
    schedule_work(&c->interval_work);
}

static void clock_signaling_work(struct ksz_schedule_work *work)
{
    struct ptp_clock *c = work->dev;
    struct ptp_port *p = clock_slave_port(c);
    int ann = 127;
    int i;

    if (!p || !(p->state == PTP_S_SLAVE || p->state == PTP_S_UNCALIBRATED))
        return;

    if (c->req_interval) {
            struct ptp_message *m = NULL;
            struct ptp_port *h = clock_host_port(c);

            m = alloc_msg(NULL, 100, NULL);
            if (!m)
                return;
            if (!c->no_announce)
                ann = c->logAnnounceInterval;
            tx_msg_lock();
            port_tx_signal_interval(p, 127, h->logSyncInterval, ann, m);
            tx_msg_unlock();
dbg_msg("tx: %d\n", h->logSyncInterval);
    }

    if (c->oper_sync_jiffies) {
        uint diff = time_delta(c->oper_sync_jiffies, jiffies);

        if (diff >= msecs_to_jiffies(p->waitSyncInterval * 1000)) {
            /* Timer is reached */
            c->oper_sync_jiffies = 0;
            if (p->logSyncInterval != p->operSyncInterval) {
#ifdef NOTIFY_PTP_EVENT
                exception_log(0, 0, "Change operLogSyncInterval %d to %d",
                    p->logSyncInterval, p->operSyncInterval);
#endif
                p->logSyncInterval = p->operSyncInterval;
                p->ds.logSyncInterval = p->operSyncInterval;
                ptp_change_slave_interval(c, p->operSyncInterval, false);
            }

            /* Interval may not be changed and so need to stop the timer. */
            if (!c->req_interval)
                clock_stop_signaling_timer(c);
        }
    }

    /* Slave port may be lost. */
    if (port_is_aed(p) && !c->first_sync) {
        clock_stop_signaling_timer(c);
    }
}

static void clock_signaling_func(TimerHandle_t xTimer)
{
    struct ksz_timer_info *timer = (struct ksz_timer_info *)
        pcTimerGetName(xTimer);
    struct ptp_clock *c = timer->param;
    struct ptp_port *h = clock_host_port(c);

dbg_msg("S\n");
    /* Catch when the port is shutting off. */
    if (h->state == PTP_S_DISABLED)
        return;
    schedule_delayed_work(&c->signaling_work, 0);
}

void clock_set_initial_sync_timeout(struct ptp_clock *c, int delay)
{
    c->sync_timeout = 0;
    c->sync_timeout_info.period = msecs_to_jiffies(delay);
    ksz_start_timer(&c->sync_timeout_info,
        c->sync_timeout_info.period);
}

void clock_set_sync_timeout(struct ptp_clock *c)
{
    struct ptp_port *h = &c->host_port;
    int delay = ptp_interval_to_ms(h->logSyncInterval);

    if (c->sync_timeout) {
        clock_stop_sync_timer(c);
    }
    if (!c->syncReceiptTimeout)
        return;
    delay *= c->syncReceiptTimeout;
    c->sync_timeout_info.period = msecs_to_jiffies(delay);
    ksz_start_timer(&c->sync_timeout_info,
        c->sync_timeout_info.period);
    time_save(&c->set_sync_timeout_jiffies, 0);
}

void clock_set_fup_timeout(struct ptp_clock *c)
{
    /* Regular sync timer is running. */
    if (c->sync_timer_info.max)
        return;

    /* Need to stop manually by receving valid Follow_Up. */
    if (c->fup_timeout_info.max)
        return;

    c->fup_timeout_info.period = msecs_to_jiffies(c->fupReceiptTimeout);
    ksz_start_timer(&c->fup_timeout_info,
        c->fup_timeout_info.period);
}

void clock_stop_sync_timeout(struct ptp_clock *c)
{
    ksz_stop_timer(&c->sync_timeout_info);
}

void clock_stop_fup_timeout(struct ptp_clock *c)
{
    ksz_stop_timer(&c->fup_timeout_info);
}

static void clock_update_interval_work(struct ksz_schedule_work *work)
{
    struct ptp_clock *c = work->dev;

#ifdef DBG_PTP_FWD
dbg_msg(" clock update inter"NL);
#endif
    ptp_set_sync_interval(c);
}

static void clock_ethernet_ready_work(struct ksz_schedule_work *work)
{
    struct aed_test_status *msg = (struct aed_test_status *)
        &eth_avtpdu_buf[14];
    struct ptp_clock *c = work->dev;
    struct ptp_port *p;
    int i;

    if (!automotive_test_mode)
        return;

    for (i = 1; i <= c->cnt; i++) {
        p = c->ptp_ports[i];
        if (p->report_ready) {
            p->report_ready = 0;
            p->report_ready_done = 1;
            update_aed_test_status(msg, avtpdu_seqid++,
                AED_DESC_TYPE_AVB_INTERFACE, p->index - 1, 0,
                AED_STATE_ETHERNET_READY);
            send_aed(msg, 1 << (p->index - 1));
dbg_msg("aed: %d\n", p->index);
        }
    }
    c->report_link = false;
}

void clock_enable_port(struct ptp_clock *c, struct ptp_port *p, int enable)
{
    u16 ports = 1 << (p->index - 1);

#ifdef DBG_PTP_PORT
dbg_msg(" e: %d=%d"NL, p->index, enable);
#endif
    if (enable)
        c->live_ports |= ports;
    else
        c->live_ports &= ~ports;
    c->host_port.portmap = c->portmap & c->live_ports;
}

u8 clock_class(struct ptp_clock *c)
{
    return c->dds.clockQuality.clockClass;
}

u16 clock_steps(struct ptp_clock *c)
{
    return c->cur.stepsRemoved;
}

struct bmc_dataset *clock_default_ds(struct ptp_clock *c)
{
    struct bmc_dataset *ds = &c->dataset;
    struct ptp_default_ds *dds= &c->dds;

    ds->prio_1 = dds->priority1;
    memcpy(&ds->id, &dds->clockIdentity, sizeof(struct ptp_clock_identity));
    memcpy(&ds->quality, &dds->clockQuality, sizeof(struct ptp_clock_quality));
    ds->prio_2 = dds->priority2;
    ds->steps_removed = 0;
    memcpy(&ds->rx.clockIdentity, &dds->clockIdentity,
        sizeof(struct ptp_clock_identity));
    memcpy(&ds->tx.clockIdentity, &dds->clockIdentity,
        sizeof(struct ptp_clock_identity));
    ds->tx.port = 0;
    return ds;
}  /* clock_default_ds */

struct bmc_dataset *clock_best_ds(struct ptp_clock *c)
{
    return c->best ? &c->best->dataset : NULL;
}

struct ptp_port *clock_best_port(struct ptp_clock *c)
{
    return c->best ? c->best->port : NULL;
}

struct ptp_port *clock_host_port(struct ptp_clock *c)
{
    return &c->host_port;
}

struct ptp_port *clock_slave_port(struct ptp_clock *c)
{
    return c->slave_port;
}

void clock_set_slave_port(struct ptp_clock *c, struct ptp_port *p)
{
    c->slave_port = p;
}

static void clock_update_grandmaster(struct ptp_clock *c)
{
    struct ptp_default_ds *ds = &c->dds;
    struct ptp_parent_ds *pds = &c->pds;

    memset(&c->cur, 0, sizeof(struct ptp_current_ds));
    memcpy(&pds->parentPortIdentity.clockIdentity, &ds->clockIdentity,
        sizeof(struct ptp_clock_identity));
    pds->parentPortIdentity.port = 0;
    memcpy(&pds->grandmasterIdentity, &ds->clockIdentity,
        sizeof(struct ptp_clock_identity));
    memcpy(&pds->grandmasterClockQuality, &ds->clockQuality,
        sizeof(struct ptp_clock_quality));
    pds->grandmasterPriority1 = ds->priority1;
    pds->grandmasterPriority2 = ds->priority2;
}  /* clock_update_grandmaster */

void clock_change_sync_timer(struct ptp_clock *c, int interval)
{
    int delay;
    int ms;
    uint ticks;

    if (interval == c->logSyncInterval)
        return;
    c->logSyncInterval = interval;
    if (c->logSyncInterval <= c->logAnnounceInterval) {
        delay = c->logSyncInterval;
        c->sync_max = 0;
        c->ann_max = (1 << (c->logAnnounceInterval - c->logSyncInterval)) - 1;
    } else {
        delay = c->logAnnounceInterval;
        c->ann_max = 0;
        c->sync_max = (1 << (c->logSyncInterval - c->logAnnounceInterval)) - 1;
dbg_msg("sync_max 0 %d %d %d"NL, c->sync_max, c->logSyncInterval, c->logAnnounceInterval);
    }
    if (c->sync_cnt > c->sync_max)
        c->sync_cnt = c->sync_max;
    if (c->ann_cnt > c->ann_max)
        c->ann_cnt = c->ann_max;

    ticks = msecs_to_jiffies(ptp_interval_to_ms(delay));
    if (c->sync_timer_info.period != ticks) {
        uint next = ticks;

        /* First trigger is about 11 ms later if request change.  Need to
         * start first Sync transmission sooner.
         */
        if (c->last_sync_time_jiffies > 1) {
            uint diff = time_delta(c->last_sync_time_jiffies, jiffies);

            if (diff < ticks)
                next = ticks - diff;
            else
                next = 10;
        }
        c->last_sync_time_jiffies = 1;
        c->sync_timer_info.period = ticks;
        if (c->sync_timer_info.max)
            ksz_change_timer(&c->sync_timer_info, next, ticks);
    }
}

void clock_start_sync_timer(struct ptp_clock *c)
{
    uint next = c->sync_timer_info.period;

    /* Sync timer is already armed. */
    if (c->tx_sync) {
#ifdef DBG_PTP_TIMER
dbg_msg(" sync started: %u"NL, c->sync_timer_info.period);
#endif
        return;
    }
#ifdef DBG_PTP_TIMER
dbg_msg(" start sync: %u"NL, c->sync_timer_info.period);
#endif

    /* Send Announce first before Sync. */
    c->ann_cnt = c->ann_max;
    c->sync_cnt = 0;

    /* Start forwarding last Sync. */
    if (c->sync_timeout) {
        c->ann_cnt = 0;
        c->sync_cnt = c->sync_max;
    }

    /* Indicate arming Sync timer for sending Sync. */
    c->tx_sync = 1;
    c->last_sync_time_jiffies = 0;

    /* This function is called later during Follow_Up timeout. */
    if (c->sync_timeout_ready) {

        /* Start the first timer faster. */
        if (c->fup_timeout_info.period < c->sync_timer_info.period) {
            next -= c->fup_timeout_info.period;
            c->last_sync_time_jiffies = 1;
        }
    }
    ksz_start_timer(&c->sync_timer_info, c->sync_timer_info.period);
    if (c->sync_timer_info.repeat)
        ksz_change_timer(&c->sync_timer_info, next,
                         c->sync_timer_info.period);
}

void clock_stop_sync_timer(struct ptp_clock *c)
{
    ksz_stop_timer(&c->sync_timer_info);

    /* No need to stop timer when receiving Sync after Sync timeout. */
    c->miss_sync = 0;
    c->sync_timeout = 0;
    c->sync_timeout_ready = 0;

    /* Indicate not arming Sync timer for sending Sync. */
    c->tx_sync = 0;
#ifdef DBG_PTP_TIMER
dbg_msg(" stop sync"NL);
#endif
}

void clock_start_signaling_timer(struct ptp_clock *c, bool oper)
{
    uint next = c->signaling_timer_info.period;
    struct ptp_port *h = clock_host_port(c);

#ifdef DBG_PTP_TIMER
dbg_msg(" start signaling: %u %u"NL, c->signaling_timer_info.period, oper);
#endif

    /* There are 2 situations that this function is called to start the
     * Signaling timer: first is to start the 1-second timer so that
     * Signaling message is sent for interval change in case the initial one
     * is not received or accepted, and second is to start the timer to switch
     * to the operational Sync interval.  This timer can be cutoff by the
     * first one, but will continue until the second one is reached.
     */
    /* This timer is used for initial Sync interval change. */
#if 0
    if (c->switch_interval && !c->req_interval) {
#endif
    if (oper) {
        struct ptp_port *p = clock_slave_port(c);

        if (!p)
            return;
        next = msecs_to_jiffies(p->waitSyncInterval * 1000);
    } else {
        c->req_interval = true;
    }

    if (c->signaling_timer_info.max)
        ksz_change_timer(&c->signaling_timer_info, next,
            c->sync_timer_info.period);
    else
        ksz_start_timer(&c->signaling_timer_info, next);
    c->signaling = 1;
}

void clock_stop_signaling_timer(struct ptp_clock *c)
{
    ksz_stop_timer(&c->signaling_timer_info);
    c->oper_sync_jiffies = 0;
    c->req_interval = false;
    c->signaling = 0;
}

static void setup_ptp_ports(struct ptp_clock *c, u16 ports)
{
    int i;
    int n;
    u8 cnt = 0;
    u16 id;
    struct ptp_port *p;

    for (i = 0, n = 1; n <= MAX_PTP_PORTS; i++, n++) {
        if (!(ports & (1 << i)))
            continue;
        p = get_ptp_hw_port(i);
        id = cnt + 1;
        c->ptp_ports[id] = p;
        p->c = c;
        init_ptp_port_hw(p, id, n);
        cnt++;
    }
    c->ports = ports;
    p = &c->host_port;
    c->ptp_ports[0] = p;
    p->c = c;
    init_ptp_port_hw(p, cnt + 1, 0);
    c->portmap = ports;
    p->portmap = ports;
    c->cnt = cnt;
#ifdef DBG_PTP_PORT
dbg_msg(" setup_ports: %d %x"NL, cnt, p->portmap);
#endif
}  /* setup_ptp_ports */

static void clock_setup_ptp_msg(struct ptp_clock *c)
{
    struct ptp_msg *msg;

    msg = (struct ptp_msg *) &c->ann_template;
    ptp_init_announce(c, msg);

    msg = (struct ptp_msg *) &c->sync_template;
    ptp_init_sync(c, msg);

    msg = (struct ptp_msg *) &c->fup_template;
    ptp_init_follow_up(c, msg);

    msg = (struct ptp_msg *) &c->delay_req_template;
    ptp_init_delay_req(c, msg);

    msg = (struct ptp_msg *) &c->delay_resp_template;
    ptp_init_delay_resp(c, msg);

    msg = (struct ptp_msg *) &c->pdelay_req_template;
    ptp_init_pdelay_req(c, msg);

    msg = (struct ptp_msg *) &c->manage_template;
    ptp_init_manage(c, msg);

    msg = (struct ptp_msg *) &c->signal_template;
    ptp_init_signal(c, msg);
}  /* clock_setup_ptp_msg */

static void clock_delete_ptp_msg(struct ptp_clock *c)
{
    msg_clear(&c->last_sync);
    msg_clear(&c->last_fup);
    msg_clear(&c->last_rx_fup);
    msg_clear(&c->last_ann);
    msg_clear(&c->base_sync);
    msg_clear(&c->base_fup);
}  /* clock_delete_ptp_msg */

static void clock_init_profile(struct ptp_clock *c)
{
    u8 major, minor;

    c->specific = 0;
    c->transport = 0;

    c->logAnnounceInterval = 1;
    c->announceReceiptTimeout = 3;

    c->logSyncInterval = 0;

    /* No Sync receive timeout. */
    c->syncReceiptTimeout = 0;
    c->fupReceiptTimeout = 25;
    c->no_announce = 0;
    gptp_get_follow_up_timeout(&c->fupReceiptTimeout);
    if (c->fupReceiptTimeout > 25)
        c->fupReceiptTimeout = 25;
    gptp_profile_get_profile(c->cfg_index, &ptp_stack_gptp);
    gptp_profile_get_e2e_p2p(c->cfg_index, &ptp_stack_e2e, &ptp_stack_p2p);
    gptp_profile_get_gm_capable(c->cfg_index, &ptp_stack_gm_capable);
    gptp_profile_get_two_step(c->cfg_index, &ptp_stack_2step);
    if (ptp_config_get_gptp_mode() > 0) {
        c->specific = 1;
        c->logAnnounceInterval = 0;
        c->logSyncInterval = -3;
        c->syncReceiptTimeout = 3;
    }
    if (ptp_config_using_p2p())
        c->delay_mechanism = DELAY_P2P;
    else if (ptp_config_using_e2e())
        c->delay_mechanism = DELAY_E2E;
    gptp_port_profile_get_sync(c->cfg_index,
        &c->logSyncInterval,
        &c->syncReceiptTimeout);
    gptp_port_profile_get_announce(c->cfg_index,
        &c->logAnnounceInterval,
        &c->announceReceiptTimeout);
    major = c->specific;
    gptp_profile_get_sdoid(c->cfg_index, &major, &minor);
    c->specific = major;
    gptp_profile_get_transport(c->cfg_index, &c->transport);
    if (ptp_config_get_gptp_mode() > 1) {
        c->no_announce = 1;

        /* Make it bigger to not interfere Sync interval calculation. */
        c->logAnnounceInterval = 4;
    }
}  /* clock_init_profile */

static void clock_init_default_ds(struct ptp_clock *c)
{
    ptp_init_config_ds(c->cfg_index, &c->dds);
    memcpy(&c->dds.clockIdentity, &c->id,
        sizeof(struct ptp_clock_identity));
}  /* clock_init_default_ds */

static void setup_ptp_port_profile(struct ptp_clock *c)
{
    signed char delay = 0;
    int i;
    u8 master;
    struct ptp_port *h;
    struct ptp_port *p;
    int two_step = (c->dds.flags & DDS_TWO_STEP_FLAG) ? 1 : 0;

    clock_setup_ptp_msg(c);
    if (ptp_config_using_p2p())
        gptp_port_profile_get_pdelay(c->cfg_index,
            &delay);
    else
        gptp_port_profile_get_delay(c->cfg_index,
            &delay);
    h = &c->host_port;
    init_ptp_port_profile(h, DELAY_NONE, two_step, delay);
    for (i = 1; i <= c->cnt; i++) {
        p = c->ptp_ports[i];
        master = 1;
        if (!gptp_port_get_master(p->index, &master)) {
            if (i == 1)
                ptp_stack_slave_port = 0;
            if (!master && !ptp_stack_slave_port)
                ptp_stack_slave_port = p->index;
        }
        init_ptp_port_profile(p, c->delay_mechanism, two_step, delay);
        memcpy(&p->ds.portIdentity.clockIdentity, &c->dds.clockIdentity,
            sizeof(struct ptp_clock_identity));
    }
    if (c->delay_mechanism == DELAY_NONE)
        filter_set_delay(&h->pdelay_info.filter, 0);
}

static void setup_ptp_hw_clock(struct ptp_clock *c)
{
    void *fd = dev[1].fd;
    int drift;
    int rc;

    rc = get_freq(fd, &drift, c->index);
    if (!rc)
        c->servo.drift = drift * 1000;
}

static void setup_ptp_clock(struct ptp_clock *c, u16 ports)
{
    clock_init_profile(c);
    clock_init_default_ds(c);
    clock_update_grandmaster(c);

    /* Only setup hardware related things once. */
    if (c->ports != ports)
        setup_ptp_ports(c, ports);
    setup_ptp_port_profile(c);
    setup_ptp_hw_clock(c);

    c->dds.numberPorts = c->cnt;
    c->dds.domainNumber = c->domain;
    ptp_update_parent_ds(c, NULL);
    c->setup = 1;
}

static struct ptp_clock *create_ptp_clock(u8 index, u8 domain, u16 ports,
                                          u16 vid)
{
    struct ptp_clock *c;
    int i;

    /* First clock is required for use with second chip in cascade mode. */
    c = &ptp_clocks[index - 1];
    if (c->domain == 255) {
        c->cfg_index = index;
        c->domain = domain;
        c->vid = vid;
        setup_ptp_clock(c, ports);
        return c;
    }
    for (i = 0; i < MAX_PTP_CLOCKS; i++) {
        c = &ptp_clocks[i];
        if (c->domain == 255) {
            c->cfg_index = index;
            c->domain = domain;
            c->vid = vid;
            setup_ptp_clock(c, ports);
            return c;
        }
    }
    return NULL;
}

void clock_stop(struct ptp_clock *c)
{
    int i;
    struct ptp_port *p;

    clock_stop_sync_timer(c);
    ksz_stop_timer(&c->sync_timeout_info);
    ksz_stop_timer(&c->fup_timeout_info);
    ksz_stop_timer(&c->interval_timer_info);
    for (i = 0; i <= c->cnt; i++) {
        p = c->ptp_ports[i];
        port_set_state(p, PTP_S_DISABLED);
    }
    if (c->signaling)
        clock_stop_signaling_timer(c);

    /* Clear last received Sync when using gPTP. */
    memset(&c->last_sync_rx, 0, sizeof(c->last_sync_rx));
    c->last_sync_jiffies = 0;
    c->stop = 1;
    c->setup = 0;
}

static void delete_ptp_clock(struct ptp_clock *c)
{
    int i;
    struct ptp_port *p;

    clock_delete_ptp_msg(c);
    for (i = 0; i <= c->cnt; i++) {
        p = c->ptp_ports[i];
        exit_ptp_port_profile(p);
        exit_ptp_port_hw(p);
    }
}

static void reset_ptp_clock(struct ptp_clock *c)
{
    int i;
    struct ptp_port *p;

    clock_delete_ptp_msg(c);
    for (i = 0; i <= c->cnt; i++) {
        p = c->ptp_ports[i];
        exit_ptp_port_profile(p);
    }

    /* Reset to initial empty value. */
    memset(&c->best_id, 0, sizeof(struct ptp_clock_identity));
}

static void inc_mac_id(u8 *addr, u8 inc)
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

static void init_ptp_clock(struct ptp_clock *c, u8 *macaddr)
{
    c->id.addr[0] = macaddr[0];
    c->id.addr[1] = macaddr[1];
    c->id.addr[2] = macaddr[2];
    c->id.addr[3] = 0xFF;
    c->id.addr[4] = 0xFE;
    c->id.addr[5] = macaddr[3];
    c->id.addr[6] = macaddr[4];
    c->id.addr[7] = macaddr[5];
    inc_mac_id(&c->id.addr[5], c->index);
/* Workaround for 1AS.com.6.5a test case. */
#if 0
    inc_mac_id(&c->id.addr[5], 1);
#endif

    c->domain = 255;
    c->servo_state = SERVO_UNLOCKED;
    init_ptp_servo(&c->servo);

    c->delay_mechanism = DELAY_NONE;

    c->utc_offset = 37;
    c->time_source = 0xA0;
    do {
        u8 data;

        if (!gptp_get_utc(&data))
            c->utc_offset = data;
    } while (0);

    init_work(&c->sync_work, c, clock_sync_work);
    init_work(&c->sync_timeout_work, c, clock_sync_timeout_work);
    init_work(&c->fup_timeout_work, c, clock_fup_timeout_work);
    init_work(&c->interval_work, c, clock_interval_work);
    init_work(&c->signaling_work, c, clock_signaling_work);
    init_work(&c->update_interval_work, c, clock_update_interval_work);
    init_work(&c->ethernet_ready_work, c, clock_ethernet_ready_work);
}  /* init_ptp_clock */

static void init_ptp_clocks(u8 *macaddr)
{
    struct ptp_clock *c;
    int i;

    for (i = 0; i < MAX_PTP_CLOCKS; i++) {
        c = &ptp_clocks[i];
        c->index = i;
        init_ptp_clock(c, macaddr);
    }
}

static void clock_start(struct ptp_clock *c)
{
    struct ptp_port *p;
    int i;
    int delay;

    if (c->stop) {
        c->stop = 0;
        if (!c->setup)
            setup_ptp_clock(c, c->ports);
    }
    if (c->logSyncInterval <= c->logAnnounceInterval) {
        delay = c->logSyncInterval;
        c->sync_max = 0;
        c->ann_max = (1 << (c->logAnnounceInterval - c->logSyncInterval)) - 1;
    } else {
        delay = c->logAnnounceInterval;
        c->ann_max = 0;
        c->sync_max = (1 << (c->logSyncInterval - c->logAnnounceInterval)) - 1;
    }

    /* Send Announce first before Sync. */
    c->ann_cnt = c->ann_max;
    c->sync_cnt = 0;

#ifdef LINUX_PTP
#if 0
    cfg_timer_dbg(TRUE);
#endif
#endif
    ksz_init_timer(&c->sync_timer_info, "clock_sync_timer",
        msecs_to_jiffies(ptp_interval_to_ms(delay)), clock_sync_func,
        NULL, c, pdTRUE);
    ksz_init_timer(&c->sync_timeout_info, "clock_sync_timeout",
        msecs_to_jiffies(ptp_interval_to_ms(delay)), clock_sync_timeout_func,
        NULL, c, pdFALSE);
    ksz_init_timer(&c->fup_timeout_info, "clock_fup_timeout",
        msecs_to_jiffies(c->fupReceiptTimeout), clock_fup_timeout_func,
        NULL, c, pdFALSE);
    ksz_init_timer(&c->interval_timer_info, "clock_interval_timer",
        msecs_to_jiffies(ptp_interval_to_ms(delay) + SYNC_MISS_MARGIN),
        clock_interval_func,
        NULL, c, pdTRUE);
    ksz_init_timer(&c->signaling_timer_info, "clock_signaling_timer",
        msecs_to_jiffies(1000), clock_signaling_func,
        NULL, c, pdTRUE);
#ifdef LINUX_PTP
#if 1
    cfg_timer_dbg(FALSE);
#endif
#endif

    for (i = 0; i <= c->cnt; i++) {
        p = c->ptp_ports[i];
        port_init(p);

        /* State will be PTP_S_FAULTY or PTP_S_LISTENING in regular profile.
         * State can be PTP_S_MASTER, PTP_S_PRE_MASTER, or PTP_S_SLAVE for
         * Automotive Profile.
         */
        port_set_state(p, PTP_S_INITIALIZING);
    }
    if (c->no_announce) {
        struct ptp_port *h = &c->host_port;
        int state = PTP_S_MASTER;

        /* No Sync when not GM capable. */
        if (clock_class(c) == 255)
            state = PTP_S_PRE_MASTER;

        /* Find if there is a slave port. */
        for (i = 1; i <= c->cnt; i++) {
            p = c->ptp_ports[i];
            if (p->gptp_slave) {
                state = PTP_S_SLAVE;
                break;
            }
        }
        port_set_state(h, state);
    }
    handle_state_decision(c);
}
