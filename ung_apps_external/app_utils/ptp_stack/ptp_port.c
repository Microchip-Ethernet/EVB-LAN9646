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

#include "ptp_port.h"
#include "ptp_clock.h"
#include "ptp_config.h"
#include "ptp_forward.h"
#include "ptp_protocol.h"
#include <string.h>

#ifdef LINUX_PTP
static pthread_mutex_t port_lock = PTHREAD_MUTEX_INITIALIZER;
#else
static struct mutex port_lock;
#endif
static struct ptp_port ptp_hw_ports[MAX_PTP_PORTS];

struct ptp_port *get_ptp_hw_port(u8 port)
{
	mutex_lock(&port_lock);
    struct ptp_port *ptr =  &ptp_hw_ports[port];
	mutex_unlock(&port_lock);
	return ptr;
}

struct ptp_port *get_ptp_port(u8 port)
{
    int i;

    for (i = 0; i < MAX_PTP_PORTS; i++) {
        if (ptp_hw_ports[i].index == port) {
			return get_ptp_hw_port(i);
        }
    }
    return NULL;
}

static void port_delay_req_work(struct ksz_schedule_work *work)
{
    struct ptp_port *p = work->dev;

    tx_msg_lock();
    port_tx_delay_req(p);
    tx_msg_unlock();
}

static void port_pdelay_req_work(struct ksz_schedule_work *work)
{
    struct ptp_port *p = work->dev;

    tx_msg_lock();
    port_tx_pdelay_req(p);
    tx_msg_unlock();
}

static void port_delay_req_func(TimerHandle_t xTimer)
{
    struct ksz_timer_info *timer = (struct ksz_timer_info *)
        pcTimerGetName(xTimer);
    struct ptp_port *p = timer->param;

    /* Catch when the port is shutting off. */
    if (p->state == PTP_S_DISABLED)
        return;
    schedule_work(&p->delay_req_work);
}

static void port_pdelay_req_func(TimerHandle_t xTimer)
{
    struct ksz_timer_info *timer = (struct ksz_timer_info *)
        pcTimerGetName(xTimer);
    struct ptp_port *p = timer->param;
    int ms;

    /* Catch when the port is shutting off. */
    if (p->state == PTP_S_DISABLED)
        return;
    ms = ptp_interval_to_ms(p->pdelay_req_interval);

#if 1
    /* Change interval when only have response. */
    /* This does not work as the tool does not send any response while testing
     * operLogPdelayReqInterval operation.
     */
    if (p->cnt_delay_req > 0 && p->peer_resp) {
#else
    if (p->cnt_delay_req > 0) {
#endif
        p->cnt_delay_req--;
        if (!p->cnt_delay_req &&
            p->operPdelayReqInterval != p->pdelay_req_interval) {
            p->pdelay_req_interval = p->operPdelayReqInterval;
            p->ds.logMinPdelayReqInterval = p->operPdelayReqInterval;
            ms = ptp_interval_to_ms(p->pdelay_req_interval);
#ifdef NOTIFY_PTP_EVENT
            exception_log(0, 0, "Pdelay interval changes to %d at p-%u",
                p->pdelay_req_interval, p->id);
#endif
        }
    }
    ms = ptp_random_ms(ms, 10);
    timer->period = msecs_to_jiffies(ms);
    ksz_start_timer(timer, timer->period);
    schedule_work(&p->delay_req_work);
}

static void port_ann_timeout_work(struct ksz_schedule_work *work)
{
    struct ptp_port *p = work->dev;

    port_announce_timeout(p);
    if (p->index) {
        port_set_state(p, port_event(p, EVENT_ANNOUNCE_TIMEOUT));
    } else {

        /* Set state later in state decision. */
        p->next_state = host_event(p, EVENT_ANNOUNCE_TIMEOUT);
    }
    handle_state_decision(p->c);
}

static void port_ann_timeout_func(TimerHandle_t xTimer)
{
    struct ksz_timer_info *timer = (struct ksz_timer_info *)
        pcTimerGetName(xTimer);
    struct ptp_port *p = timer->param;

    /* Catch when the port is shutting off. */
    if (p->state == PTP_S_DISABLED)
        return;
    schedule_delayed_work(&p->ann_timeout_work, 0);
}

static void port_qual_timeout_work(struct ksz_schedule_work *work)
{
    struct ptp_port *p = work->dev;

    if (!p->index)
        return;
    port_set_state(p, port_event(p, EVENT_QUALIFICATION_TIMEOUT));
    handle_state_decision(p->c);
}

static void port_qual_timeout_func(TimerHandle_t xTimer)
{
    struct ksz_timer_info *timer = (struct ksz_timer_info *)
        pcTimerGetName(xTimer);
    struct ptp_port *p = timer->param;

    /* Catch when the port is shutting off. */
    if (p->state == PTP_S_DISABLED)
        return;
    schedule_delayed_work(&p->qual_timeout_work, 0);
}

static void port_fault_timeout_work(struct ksz_schedule_work *work)
{
    struct ptp_port *p = work->dev;

    p->last_fault = 0;
    if (p->index && !p->no_link) {
        port_set_state(p, port_event(p, EVENT_FAULT_CLEAR));
        handle_state_decision(p->c);
    }
}

static void port_fault_timeout_func(TimerHandle_t xTimer)
{
    struct ksz_timer_info *timer = (struct ksz_timer_info *)
        pcTimerGetName(xTimer);
    struct ptp_port *p = timer->param;

    /* Catch when the port is shutting off. */
    if (p->state == PTP_S_DISABLED)
        return;
    schedule_delayed_work(&p->fault_timeout_work, 0);
}

int port_state(struct ptp_port *p)
{
    return p->state;
}

bool port_is_aed(struct ptp_port *p)
{
    return (p->gptp_auto == 1);
}

bool port_is_aed_master(struct ptp_port *p)
{
    return (p->gptp_auto == 1 && p->gptp_slave == 0);
}

struct bmc_dataset *port_best_ds(struct ptp_port *p)
{
    return p->best ? &p->best->dataset : NULL;
}

static void init_nrate_info(struct nrate_info *nrate)
{
    nrate->ratio = 1.0;
    ptu_zero(&nrate->tx1);
    ptu_zero(&nrate->tx2);
    ptu_zero(&nrate->rx1);
    ptu_zero(&nrate->rx2);
    nrate->ratio_valid = 0;
}

void init_ptp_port_profile(struct ptp_port *p, u8 delay_mechanism,
    int two_step, int delay)
{
    void *fd = dev[1].fd;
    int p2p = 0;
    int rc;

    p->sync_two_step = ptp_config_using_two_step();
    p->p2p_two_step = ptp_config_using_two_step();
    p->gptp = 0;
    p->gptp_auto = 0;
    p->no_id_check = 0;
    p->no_id_replace = 1;
    p->path_trace = 0;
    p->follow_up_tlv = 0;
    p->as_capable = 0;
    p->as_capable_set = 0;
    p->no_capable = 1;
    p->use_delay = 1;
    p->gptp_slave = 0;
    if (ptp_config_get_gptp_mode() > 0) {
        p->gptp = 1;
        p->no_id_replace = 0;
        p->path_trace = 1;
        p->follow_up_tlv = 1;
        p->no_capable = 0;
        if (ptp_config_get_gptp_mode() > 1) {
            u8 slave_port = ptp_config_get_slave_port();

            p->gptp_auto = 1;
            p->no_id_check = 1;
            p->no_capable = 1;

            /* Manually set this port as slave. */
            if (p->index && p->index == slave_port)
                p->gptp_slave = 1;
        }
    }

    p->logSyncInterval = p->c->logSyncInterval;
    if (p->index) {
        ptp_init_port_ds(p);
        p->logSyncInterval = p->ds.logSyncInterval;
    }
    p->initialSyncInterval = p->logSyncInterval;
    p->operSyncInterval = p->logSyncInterval + 1;

    p->ds.delayMechanism = delay_mechanism;

    p->delay_interval = delay;
    p->initialPdelayReqInterval = delay;
    p->operPdelayReqInterval = delay + 1;
    p->pdelay_req_interval = p->initialPdelayReqInterval;

    p->initialSyncTimeout = 2;
    p->waitSyncInterval = 5;
    p->waitPdelayReqInterval = 10;

    gptp_port_profile_get_oper(p->c->cfg_index,
        &p->operSyncInterval,
        &p->operPdelayReqInterval);
    do {
        u8 initial, sync, pdelay;

        if (!gptp_get_initial_wait(&initial, &sync, &pdelay)) {
            p->initialSyncTimeout = initial;
            p->waitSyncInterval = sync;
            p->waitPdelayReqInterval = pdelay;
        }
    } while (0);
    if (delay_mechanism == DELAY_E2E) {
        init_work(&p->delay_req_work, p, port_delay_req_work);
        ksz_init_timer(&p->delay_timer_info, "port_delay_timer",
            msecs_to_jiffies(ptp_interval_to_ms(delay)), port_delay_req_func,
            NULL, p, pdFALSE);
    } else if (delay_mechanism == DELAY_P2P) {
        init_work(&p->delay_req_work, p, port_pdelay_req_work);
        ksz_init_timer(&p->delay_timer_info, "port_pdelay_timer",
            msecs_to_jiffies(ptp_interval_to_ms(delay)), port_pdelay_req_func,
            NULL, p, pdFALSE);
        p2p = 1;
    }

    init_work(&p->ann_timeout_work, p, port_ann_timeout_work);
    ksz_init_timer(&p->ann_timeout_timer_info, "port_ann_timeout_timer",
        msecs_to_jiffies(ptp_interval_to_ms(delay)), port_ann_timeout_func,
        NULL, p, pdFALSE);

#ifdef LINUX_PTP
#if 1
    cfg_timer_dbg(TRUE);
#endif
#endif
    init_work(&p->qual_timeout_work, p, port_qual_timeout_work);
    ksz_init_timer(&p->qual_timeout_timer_info, "port_qual_timeout_timer",
        msecs_to_jiffies(ptp_interval_to_ms(delay)), port_qual_timeout_func,
        NULL, p, pdFALSE);

    init_work(&p->fault_timeout_work, p, port_fault_timeout_work);
    ksz_init_timer(&p->fault_timeout_timer_info, "port_fault_timeout_timer",
        msecs_to_jiffies(5 * 60 * 1000), port_fault_timeout_func,
        NULL, p, pdFALSE);
#ifdef LINUX_PTP
#if 1
    cfg_timer_dbg(FALSE);
#endif
#endif

    if (p->index > 0) {
        rc = set_2_domain_port_cfg(fd, p->index, two_step, p2p);
        (void) rc;
    }
}

void exit_ptp_port_profile(struct ptp_port *p)
{
    msg_info_clear(&p->tx_sync);
    msg_info_clear(&p->tx_delay_req);
    msg_info_clear(&p->tx_pdelay_resp);
    msg_info_clear(&p->rx_delay_resp);
    msg_info_clear(&p->rx_pdelay_fup);
    if (p->multi_pdelay) {
        struct ptp_msg_info *rsp = p->multi_pdelay->rx_pdelay_resp;
        struct ptp_msg_info *fup = p->multi_pdelay->rx_pdelay_fup;
        int i;

        for (i = 0; i < MAX_PDELAY_RESP; i++) {
            msg_info_clear(rsp);
            msg_info_clear(fup);
            ++rsp;
            ++fup;
        }
    }
    if (p->delay_timer_info.callback)
        ksz_exit_timer(&p->delay_timer_info);
    ksz_stop_timer(&p->ann_timeout_timer_info);
    ksz_stop_timer(&p->qual_timeout_timer_info);
    ksz_stop_timer(&p->fault_timeout_timer_info);
}

void exit_ptp_port_hw(struct ptp_port *p)
{
    struct ptp_pdelay_info *info;

    if (p->multi_pdelay) {
        int i;

        info = p->multi_pdelay->pdelay_info;
        for (i = 0; i < MAX_PDELAY_RESP; i++) {
            exit_ptp_filter(&info->filter);
            ++info;
        }
        mem_info_free(p->multi_pdelay);
        p->multi_pdelay = NULL;
    }
    info = &p->pdelay_info;
    exit_ptp_filter(&info->filter);
}

void init_ptp_multi_pdelay(struct ptp_port *p)
{
    struct ptp_pdelay_info *info;
    int i;

    info = p->multi_pdelay->pdelay_info;
    for (i = 0; i < MAX_PDELAY_RESP; i++) {
        init_ptp_filter(&info->filter);
        info->index = i + 1;
        ++info;
    }    
}

void init_ptp_port_hw(struct ptp_port *p, u16 id, u8 index)
{
    struct ptp_pdelay_info *info;

    p->ds.portIdentity.port = id;
    p->id = id;
    p->index = index;
    p->state = PTP_S_INIT;
    p->no_link = 1;
    if (index > 0) {
        p->state = PTP_S_DISABLED;
        p->portmap = (1 << (index - 1));
    }
    if (p->multi_pdelay) {
        int i;

        info = p->multi_pdelay->pdelay_info;
        for (i = 0; i < MAX_PDELAY_RESP; i++) {
            init_ptp_filter(&info->filter);
            ++info;
        }
    }
    info = &p->pdelay_info;
    init_ptp_filter(&info->filter);
}

void port_announce_timeout(struct ptp_port *p)
{
    struct foreign_dataset *f;

#ifdef DBG_PTP_TIMEOUT
dbg_msg("ann timeout: %x"NL, p->index);
#endif
    if (!p->best)
        return;
    f = p->best;
    f->valid = 0;
    f->cnt = 0;
    p->best = NULL;
}

void port_set_announce_timeout(struct ptp_port *p)
{
    int delay = ptp_interval_to_ms(p->ds.logAnnounceInterval);

    delay *= p->ds.announceReceiptTimeout;
    p->ann_timeout_timer_info.period = msecs_to_jiffies(delay);
    ksz_start_timer(&p->ann_timeout_timer_info,
        p->ann_timeout_timer_info.period);
}

/* Not used. */
void port_stop_announce_timeout(struct ptp_port *p)
{
    ksz_stop_timer(&p->ann_timeout_timer_info);
}

void port_set_qualification_timeout(struct ptp_port *p)
{
    int delay = ptp_interval_to_ms(p->ds.logAnnounceInterval);

    delay *= 1 + clock_steps(p->c);
    p->qual_timeout_timer_info.period = msecs_to_jiffies(delay);
    ksz_start_timer(&p->qual_timeout_timer_info,
        p->qual_timeout_timer_info.period);
}

void port_stop_qualification_timeout(struct ptp_port *p)
{
    ksz_stop_timer(&p->qual_timeout_timer_info);
}

void port_set_fault_timeout(struct ptp_port *p, int fault, int ms)
{
    p->last_fault = fault;
    p->fault_timeout_timer_info.period = msecs_to_jiffies(ms);
    ksz_start_timer(&p->fault_timeout_timer_info,
        p->fault_timeout_timer_info.period);
    port_set_state(p, port_event(p, EVENT_FAULT_SET));
}

void port_stop_fault_timeout(struct ptp_port *p)
{
    ksz_stop_timer(&p->fault_timeout_timer_info);
}

void port_start_sync_timer(struct ptp_port *p)
{
    if (!p->index) {
        void *fd = dev[1].fd;
        int p2p = (p->c->delay_mechanism == DELAY_P2P) ? 1 : 0;
        int two_step = (p->c->dds.flags & DDS_TWO_STEP_FLAG);
        int as = p->gptp;
        int two_domain;
        int rc;

        rc = get_2_domain_cfg(fd, &two_domain);
        if (!rc && !two_domain)
            set_global_cfg(fd, 1, two_step, p2p, as);
        clock_start_sync_timer(p->c);
    }
}

void port_stop_sync_timer(struct ptp_port *p)
{
    if (!p->index) {
        void *fd = dev[1].fd;
        int p2p = (p->c->delay_mechanism == DELAY_P2P) ? 1 : 0;
        int two_step = (p->c->dds.flags & DDS_TWO_STEP_FLAG);
        int as = p->gptp;
        int two_domain;
        int rc;

        rc = get_2_domain_cfg(fd, &two_domain);
        if (!rc && !two_domain)
            set_global_cfg(fd, 0, two_step, p2p, as);
        clock_stop_sync_timer(p->c);
    }
}

void port_set_capable(struct ptp_port *p, int capable)
{
    p->as_capable = capable ? 1 : 0;
}  /* port_set_capable */

#ifdef DBG_PTP_PEER
static void port_capable_err(struct ptp_port *p, u16 bit, char *msg)
{
    if (!(p->as_capable_err & bit)) {
        p->as_capable_err |= bit;
        dbg_msg(" %x=%s"NL, p->index, msg);
    }
}

#define AS_CAPABLE_ERR_MISS_RESP        BIT(0)
#define AS_CAPABLE_ERR_LARGE_DELAY      BIT(1)
#define AS_CAPABLE_ERR_MULT_RESP        BIT(2)
#define AS_CAPABLE_ERR_PEER_NOT_VALID   BIT(3)
#define AS_CAPABLE_ERR_RATIO_NOT_VALID  BIT(4)
#endif

void port_check_capable(struct ptp_port *p)
{
    struct ptp_pdelay_info *info = &p->pdelay_info;
    int capable = 1;
#ifdef DBG_PTP_PEER
    char msg[80];
#endif

    if (p->multi_pdelay)
        info = p->multi_pdelay->pdelay_info;

    /* Too many missing responses. */
    if (p->resp_missing >= allowed_lost_responses) {
        capable = 0;
#ifdef DBG_PTP_PEER_
        snprintf(msg, sizoef(msg), "missing: %d", p->resp_missing);
        port_capable_err(p, AS_CAPABLE_ERR_MISS_RESP, msg);
#endif
        goto done;
    }

    /* Peer delay too big. */
    if ((int) info->delay > p->neighborDelayThresh) {
        capable = 0;
#ifdef DBG_PTP_PEER
        snprintf(msg, sizeof(msg), "large delay: %d", (int)info->delay);
        port_capable_err(p, AS_CAPABLE_ERR_LARGE_DELAY, msg);
#endif
        goto done;
    }

    /* Multiple responses received. */
    if (p->mult_resp) {
        capable = 0;
#ifdef DBG_PTP_PEER
        snprintf(msg, sizeof(msg), "mult resp");
        port_capable_err(p, AS_CAPABLE_ERR_MULT_RESP, msg);
#endif
        goto done;
    }

    /* Peer Id changed. */
    if (!p->peer_valid) {
        capable = 0;
#ifdef DBG_PTP_PEER
        snprintf(msg, sizeof(msg), "peer not valid");
        port_capable_err(p, AS_CAPABLE_ERR_PEER_NOT_VALID, msg);
#endif
        goto done;
    }

    if (!info->nrate.ratio_valid) {
        capable = 0;
#ifdef DBG_PTP_PEER
        snprintf(msg, sizeof(msg), "ratio not valid");
        port_capable_err(p, AS_CAPABLE_ERR_RATIO_NOT_VALID, msg);
#endif
        goto done;
    }
    p->as_capable_err = 0;

done:
#ifdef DBG_PTP_PEER
    if (p->as_capable != capable) {
        if (capable)
dbg_msg(" delay: %x=%d"NL, p->index, (int) info->delay);
dbg_msg(" set capable: %x=%u"NL, p->index, capable);
    }
#endif
    p->as_capable = capable;
}  /* port_check_capable */

int port_capable(struct ptp_port *p)
{
    int capable = 1;

    if (!p->no_capable && !p->as_capable_set)
        capable = p->as_capable;
    return capable;
}

static void port_start_p2p(struct ptp_port *p)
{
    if (p->ds.delayMechanism == DELAY_P2P && p->use_delay) {
        ksz_start_timer(&p->delay_timer_info, p->delay_timer_info.period);

        /* Use for changing request interval. */
        if (port_is_aed(p))
            p->cnt_delay_req =
                ptp_interval_to_cnt(p->initialPdelayReqInterval,
                                    p->waitPdelayReqInterval);

        /* Use for debugging. */
        p->cnt_delay_resp = 20;
        if (p->gptp_auto)
            p->cnt_delay_resp = 10;
        p->p2p = 1;
#ifdef DBG_PTP_PEER
dbg_msg("p2p 1: %x"NL, p->index);
#endif
    }
}  /* port_start_p2p */

static void port_stop_p2p(struct ptp_port *p)
{
    if (p->ds.delayMechanism == DELAY_P2P && p->p2p) {
        ksz_stop_timer(&p->delay_timer_info);
        p->p2p = 0;

        /* Reset to initial interval when stopped due to link down. */
        p->pdelay_req_interval = p->initialPdelayReqInterval;
        p->ds.logMinPdelayReqInterval = p->initialPdelayReqInterval;
        p->delay_timer_info.period =
            msecs_to_jiffies(ptp_interval_to_ms(p->pdelay_req_interval));
    }
}  /* port_stop_p2p */

void port_init(struct ptp_port *p)
{
    if (!p->index)
        return;

    memset(p->foreign_masters, 0,
        sizeof(struct foreign_dataset) * MAX_FOREIGN_MASTERS);
    p->best = NULL;
}

static void port_to_sync(struct ptp_port *p)
{
    struct ptp_pdelay_info *info = &p->pdelay_info;

    if (p->multi_pdelay)
        info = p->multi_pdelay->pdelay_info;
    clock_peer_delay(p->c, NULL, NULL, info);
#if 0
    ptp_change_slave_interval(p->c, p->logSyncInterval, true);
#else
    ptp_change_slave_interval(p->c, p->logSyncInterval, false);
#endif
    port_set_state(clock_host_port(p->c), PTP_S_SLAVE);
}

static void port_to_master(struct ptp_port *p)
{
    if (port_is_aed(p)) {
        p->c->set_interval_force = 1;
        schedule_delayed_work(&p->c->update_interval_work,
                              msecs_to_jiffies(5));
    }
    if (!p->index) {
        port_start_sync_timer(p);
    } else {
        /* TimeTransmitter does not send Pdelay_Req. */
        if (p->gptp && p->use_one_way_resp) {
            p->as_capable_set = 1;
            port_stop_p2p(p);
        }
    }
}

static void port_to_slave(struct ptp_port *p)
{
    if (!p->index) {

        /* Initialize to invalid values.  Set to same value to start Sync
         * timeout timer in gPTP.
         */
        p->c->rx_sync_seqid = 0x10000;
        p->c->rx_fup_seqid = 0x10000;
        return;
    }

    /* Turn off Sync timer if started. */
    if (p->c->tx_sync)
        clock_stop_sync_timer(p->c);
    if (port_is_aed(p)) {
        int delay;

        delay = p->initialSyncTimeout * 1000;
        clock_set_initial_sync_timeout(p->c, delay);
    } else {
        /* Want to become master when necessary. */
        if (clock_class(p->c) != 255)
            port_set_announce_timeout(p);

        /* Start signalling timer if not already started. */
        if (p->gptp && !p->c->signaling)
            clock_start_signaling_timer(p->c, false);
    }

    /* TimeReceiver sends Pdelay_Req. */
    if (p->gptp && p->use_one_way_resp) {
        p->as_capable_set = 0;
        port_start_p2p(p);
    }
}

static void port_initialize(struct ptp_port *p)
{
    struct ptp_port_ds *ds = &p->ds;

    ptp_init_port_ds(p);

#ifdef LINUX_PTP
#if 1
    p->last_seqid_rx_ann =
    p->last_seqid_rx_sync =
    p->last_seqid_rx_fup =
    p->last_seqid_rx_delay_req =
    p->last_seqid_rx_delay_resp =
    p->last_seqid_rx_pdelay_resp_fup =
    p->last_seqid_tx_ann =
    p->last_seqid_tx_sync =
    p->last_seqid_tx_fup =
    p->last_seqid_tx_delay_req =
    p->last_seqid_tx_delay_resp =
    p->last_seqid_tx_pdelay_resp_fup = 0;
#endif
#endif

    if (p->index) {
        struct ptp_pdelay_info *info;
        u16 thresh;
        u8 pdelay;

        p->logSyncInterval = p->initialSyncInterval;
        p->neighborDelayThresh = 800;

        /* Provide information at first response. */
        p->report_resp = 1;

        /* Indicate not capable until getting a response. */
        if (!p->no_capable)
            p->resp_missing = allowed_lost_responses + 1;
        if (!p->allow_multi_resp)
            p->peer_valid = 0;
        p->peer_resp = 0;

        if (!gptp_port_get_use_delay(p->index, &pdelay)) {
            if (!pdelay) {
                p->as_capable_set = 1;
                p->use_delay = 0;
            }
        }
        if (!gptp_port_get_latency(p->index,
            &p->rx_latency,
            &p->tx_latency,
            &p->asym_delay))
            port_set_latency(p);
        if (!gptp_port_profile_get_delay_thresh(p->c->cfg_index,
            &thresh))
            p->neighborDelayThresh = thresh;

        if (p->multi_pdelay) {
            int i;

            info = p->multi_pdelay->pdelay_info;
            for (i = 0; i < MAX_PDELAY_RESP; i++) {
                info->delay = 0;
                init_nrate_info(&info->nrate);
                filter_reset(&info->filter, true);
                ++info;
            }
        }
        info = &p->pdelay_info;
        info->delay = 0;
        init_nrate_info(&info->nrate);
        filter_reset(&info->filter, true);
        if (!gptp_port_get_peer_delay(p->index, &thresh)) {
            if (thresh) {
                info->delay = thresh;
                info->raw_delay = thresh;
                p->ds.peerMeanPathDelay = info->delay << SCALED_NANOSEC_S;
                port_set_peer_delay(p);
            }
        }

        if (p->no_link) {
            p->state = PTP_S_FAULTY;
        } else {
            p->state = PTP_S_LISTENING;
            if (port_is_aed(p)) {
                p->report_announce = 1;
                p->report_mismatched = 0;
                p->report_pdelay = 1;
                if (p->gptp_slave) {
                    p->state = PTP_S_SLAVE;
                    p->report_signal = 1;
                    port_to_sync(p);
                } else {
                    p->state = PTP_S_MASTER;
                    p->report_sync = 1;
#ifdef DBG_PTP_PORT
dbg_msg(" report: %x"NL, p->index);
#endif
                }
            } else if (clock_class(p->c) != 255 && !p->last_fault) {
                port_set_announce_timeout(p);
            }
            clock_enable_port(p->c, p, 1);

            /* TimeTransmitter does not send Pdelay_Req. */
            if (!p->use_one_way_resp ||
                (p->gptp && p->state != PTP_S_MASTER))
                port_start_p2p(p);
            
            /* Apply only for Automotive Profile. */
            if (p->state == PTP_S_MASTER)
                port_to_master(p);
            else if (p->state == PTP_S_SLAVE)
                port_to_slave(p);
        }

        /* Need to start the receive sync timeout for not receiving. */
        if (p->gptp_slave) {
            if (p->state == PTP_S_FAULTY) {
                int delay;

                delay = p->initialSyncTimeout * 1000;
                delay *= 2;
                clock_set_initial_sync_timeout(p->c, delay);
            }
            clock_set_slave_port(p->c, p);
        }
    } else {
        /* Host port is set first. */
        p->state = PTP_S_LISTENING;
    }
}

int host_event(struct ptp_port *p, int event)
{
    int next = p->state;

    if (event == EVENT_BECOME_PASSIVE) {
        struct ptp_clock *c = p->c;
        struct ptp_port *q;
        bool all_faulty = true;
        int n;

        for (n = 1; n <= c->cnt; n++) {
            q = c->ptp_ports[n];
            if (q->state != PTP_S_FAULTY) {
                all_faulty = false;
                break;
            }
        }
        if (all_faulty)
            next = PTP_S_LISTENING;
        else
            next = PTP_S_PASSIVE;
        return next;
    }
    switch (p->state) {
    case PTP_S_INITIALIZING:
        switch (event) {
        case EVENT_INIT_COMPLETE:
            next = PTP_S_LISTENING;
            break;
        }
        break;
    case PTP_S_LISTENING:
    case PTP_S_PASSIVE:
        switch (event) {
        case EVENT_ANNOUNCE_TIMEOUT:
            if (clock_class(p->c) != 255)
                next = PTP_S_MASTER;
            break;
        case EVENT_BECOME_SLAVE:
            next = PTP_S_SLAVE;
            break;
        case EVENT_BECOME_MASTER:
            next = PTP_S_MASTER;
            break;
        }
        break;
    case PTP_S_MASTER:
        switch (event) {
        case EVENT_BECOME_SLAVE:
            next = PTP_S_SLAVE;
            break;
        case EVENT_BECOME_PASSIVE:
            next = PTP_S_LISTENING;
            break;
        }
        break;
    case PTP_S_SLAVE:
        switch (event) {
        case EVENT_ANNOUNCE_TIMEOUT:
            if (clock_class(p->c) != 255)
                next = PTP_S_MASTER;
            else
                next = PTP_S_LISTENING;
            break;
        case EVENT_BECOME_MASTER:
            next = PTP_S_MASTER;
            break;
        case EVENT_BECOME_PASSIVE:
            next = PTP_S_LISTENING;
            break;
        }
        break;
    }
    return next;
}  /* host_event */

int port_event(struct ptp_port *p, int event)
{
    int next = p->state;

    switch (p->state) {
    case PTP_S_INITIALIZING:
        switch (event) {
        case EVENT_INIT_COMPLETE:
            next = PTP_S_LISTENING;
            break;
        case EVENT_FAULT_SET:
            next = PTP_S_FAULTY;
            break;
        case EVENT_DISABLE:
            next = PTP_S_DISABLED;
            break;
        }
        break;
    case PTP_S_FAULTY:
        switch (event) {
        case EVENT_FAULT_CLEAR:
            next = PTP_S_INITIALIZING;
            break;
        case EVENT_DISABLE:
            next = PTP_S_DISABLED;
            break;
        }
        break;
    case PTP_S_DISABLED:
        switch (event) {
        case EVENT_ENABLE:
            next = PTP_S_INITIALIZING;
            break;
        }
        break;
    case PTP_S_LISTENING:
        switch (event) {
        case EVENT_FAULT_SET:
            next = PTP_S_FAULTY;
            break;
        case EVENT_DISABLE:
            next = PTP_S_DISABLED;
            break;
        case EVENT_ANNOUNCE_TIMEOUT:
            if (clock_class(p->c) != 255) {
                if (p->gptp)
                    next = PTP_S_MASTER;
                else
                    next = PTP_S_PRE_MASTER;
            }
            break;
        case EVENT_BECOME_SLAVE:
            next = PTP_S_UNCALIBRATED;
            break;
        case EVENT_READY_MASTER:
            if (p->gptp)
                next = PTP_S_MASTER;
            else
                next = PTP_S_PRE_MASTER;
            break;
        case EVENT_BECOME_MASTER:
            next = PTP_S_MASTER;
            break;
        case EVENT_BECOME_PASSIVE:
            next = PTP_S_PASSIVE;
            break;
        }
        break;
    case PTP_S_PRE_MASTER:
        switch (event) {
        case EVENT_FAULT_SET:
            next = PTP_S_FAULTY;
            break;
        case EVENT_DISABLE:
            next = PTP_S_DISABLED;
            break;
        case EVENT_BECOME_SLAVE:
            next = PTP_S_UNCALIBRATED;
            break;
        case EVENT_QUALIFICATION_TIMEOUT:
        case EVENT_BECOME_MASTER:
            next = PTP_S_MASTER;
            break;
        case EVENT_BECOME_PASSIVE:
            next = PTP_S_PASSIVE;
            break;
        }
        break;
    case PTP_S_MASTER:
        switch (event) {
        case EVENT_FAULT_SET:
            next = PTP_S_FAULTY;
            break;
        case EVENT_DISABLE:
            next = PTP_S_DISABLED;
            break;
        case EVENT_BECOME_SLAVE:
            next = PTP_S_UNCALIBRATED;
            break;
        case EVENT_BECOME_PASSIVE:
            next = PTP_S_PASSIVE;
            break;
        }
        break;
    case PTP_S_PASSIVE:
        switch (event) {
        case EVENT_FAULT_SET:
            next = PTP_S_FAULTY;
            break;
        case EVENT_DISABLE:
            next = PTP_S_DISABLED;
            break;
        case EVENT_BECOME_SLAVE:
            next = PTP_S_UNCALIBRATED;
            break;
        case EVENT_ANNOUNCE_TIMEOUT:
            next = PTP_S_MASTER;
            break;
        case EVENT_READY_MASTER:
            if (p->gptp)
                next = PTP_S_MASTER;
            else
                next = PTP_S_PRE_MASTER;
            break;
        case EVENT_BECOME_MASTER:
            next = PTP_S_MASTER;
            break;
        }
        break;
    case PTP_S_UNCALIBRATED:
        switch (event) {
        case EVENT_FAULT_SET:
            next = PTP_S_FAULTY;
            break;
        case EVENT_DISABLE:
            next = PTP_S_DISABLED;
            break;
        case EVENT_ANNOUNCE_TIMEOUT:
            if (clock_class(p->c) != 255)
                next = PTP_S_MASTER;
            else
                next = PTP_S_LISTENING;
            break;
        case EVENT_READY_MASTER:
            if (p->gptp)
                next = PTP_S_MASTER;
            else
                next = PTP_S_PRE_MASTER;
            break;
        case EVENT_BECOME_MASTER:
            next = PTP_S_MASTER;
            break;
        case EVENT_BECOME_PASSIVE:
            next = PTP_S_PASSIVE;
            break;
        case EVENT_CLOCK_STABLE:
            next = PTP_S_SLAVE;
            break;
        }
        break;
    case PTP_S_SLAVE:
        switch (event) {
        case EVENT_FAULT_SET:
            next = PTP_S_FAULTY;
            break;
        case EVENT_DISABLE:
            next = PTP_S_DISABLED;
            break;
        case EVENT_ANNOUNCE_TIMEOUT:
            if (clock_class(p->c) != 255)
                next = PTP_S_MASTER;
            else
                next = PTP_S_LISTENING;
            break;
        case EVENT_READY_MASTER:
            if (p->gptp)
                next = PTP_S_MASTER;
            else
                next = PTP_S_PRE_MASTER;
            break;
        case EVENT_BECOME_MASTER:
            next = PTP_S_MASTER;
            break;
        case EVENT_BECOME_PASSIVE:
            next = PTP_S_PASSIVE;
            break;
        case EVENT_CLOCK_UNSTABLE:
            next = PTP_S_UNCALIBRATED;
            break;
        }
        break;
    }
    return next;
}  /* port_event */

void port_set_state(struct ptp_port *p, int state)
{
    bool master = false;

    if (state == PTP_S_SLAVE && !p->index) {
        /* For debug purpose. */
        p->cnt_sync_rx = 20;
    }
    if (p->state == state)
        return;

    /* Stop functions when leaving state. */
    switch (p->state) {
    case PTP_S_MASTER:
        if (!p->index)
            port_stop_sync_timer(p);
        master = true;
        break;
    case PTP_S_UNCALIBRATED:
    case PTP_S_SLAVE:
        if (!p->gptp && p->c->sync_timeout) {
            p->c->sync_timeout = 0;
            clock_stop_sync_timer(p->c);
        }

        /* Do not do anything if state is still receiving Sync. */
        if (state == PTP_S_UNCALIBRATED || state == PTP_S_SLAVE)
            break;
        if (p->index && p->gptp && !port_is_aed(p))
            clock_stop_signaling_timer(p->c);
        break;
    }

#ifdef DBG_PTP_STATE
dbg_msg(" ps: %x:%d -> %d"NL, p->index, p->state, state);
#endif
    p->state = state;
    switch (state) {
    case PTP_S_INITIALIZING:
        port_initialize(p);
        break;
    case PTP_S_FAULTY:
        clock_enable_port(p->c, p, 0);
        port_stop_p2p(p);

        /* Can reduce Sync interval to one used by other ports. */
        if (port_is_aed(p) && master) {
            p->c->set_interval_force = 1;
            schedule_delayed_work(&p->c->update_interval_work,
                                  msecs_to_jiffies(5));
        }
        break;
    case PTP_S_DISABLED:
        clock_enable_port(p->c, p, 0);
        port_stop_p2p(p);
        break;
    case PTP_S_LISTENING:
        /* Initialize to invalid value to get the real interval from received
         * Sync.
         */
        if (!p->index && p->gptp) {
#ifdef DBG_PTP_FWD
dbg_msg(" int: %d"NL, p->logSyncInterval);
#endif
            p->logSyncInterval = 8;
        }

        /* Can become master. */
        if (p->index && clock_class(p->c) != 255)
            port_set_announce_timeout(p);
        break;
    case PTP_S_PASSIVE:
        if (p->index && clock_class(p->c) != 255)
            port_set_announce_timeout(p);
        break;
    case PTP_S_UNCALIBRATED:
        if (p->ds.delayMechanism == DELAY_P2P) {
            struct ptp_pdelay_info *info = &p->pdelay_info;

            if (p->multi_pdelay)
                info = p->multi_pdelay->pdelay_info;
            clock_peer_delay(p->c, NULL, NULL, info);
        }

    /* Fall through */
    case PTP_S_SLAVE:
        port_to_slave(p);
        break;
    case PTP_S_PRE_MASTER:
        if (p->index && !p->gptp)
            port_set_qualification_timeout(p);
        break; 
    case PTP_S_MASTER:
        port_to_master(p);
        break;
    }
    p->ds.portState = p->state;
    p->next_state = p->state;
}

void port_lock_init(void)
{
	mutex_init(&port_lock);
}

int32_t ptp_get_port_ds(uint16_t const port, struct ptp_port_ds *const ds)
{
	int32_t rc = -1;
	struct ptp_port *ptr = get_ptp_port(port);
	memset(ds, 0, sizeof(struct ptp_port_ds));
	if(ptr != NULL)
	{
		rc = 0;
		mutex_lock(&port_lock);
		memcpy(ds, &(ptr->ds), sizeof(struct ptp_port_ds));
		mutex_unlock(&port_lock);
	}
	return rc;
}

int32_t ptp_get_port_ds_np(uint16_t const port, uint32_t *const th, int32_t *const cap)
{
	int32_t rc = -1;
	struct ptp_port *ptr = get_ptp_port(port);
	*th = *cap = 0;
	if(ptr != NULL)
	{
		rc = 0;
		mutex_lock(&port_lock);
		*th = ptr->neighborDelayThresh;		
		*cap = 1U;
		if (!ptr->no_capable && !ptr->as_capable_set)
			*cap = ptr->as_capable;
		mutex_unlock(&port_lock);
	}
	return rc;
}

int32_t ptp_get_parent_ds(uint16_t const port, struct ptp_clock_identity *const clk_id, struct ptp_parent_ds *const ds)
{
	int32_t rc = -1;
	struct ptp_port *ptr = get_ptp_port(port);
	memset(ds, 0, sizeof(struct ptp_parent_ds));
	if(ptr != NULL)
	{
		rc = 0;
		mutex_lock(&port_lock);
		if (same_clock_id(&ptr->c->id, clk_id) || broadcast_clock_id(clk_id)) {
			struct ptp_clock *c = ptr->c;
			if(c != NULL)
			{
				rc = 0;
				memcpy(ds, &(c->pds), sizeof(struct ptp_parent_ds));
			}
		}
		mutex_unlock(&port_lock);
	}
	return rc;	
}

int32_t ptp_get_def_ds(uint16_t const port, struct ptp_clock_identity *const clk_id, struct ptp_default_ds *const ds)
{
	int32_t rc = -1;
	struct ptp_port *ptr = get_ptp_port(port);
	memset(ds, 0, sizeof(struct ptp_default_ds));
	if(ptr != NULL)
	{
		rc = 0;
		mutex_lock(&port_lock);
		if (same_clock_id(&ptr->c->id, clk_id) || broadcast_clock_id(clk_id)) {
			struct ptp_clock *c = ptr->c;
			if(c != NULL)
			{
				rc = 0;
				memcpy(ds, &(c->dds), sizeof(struct ptp_default_ds));
			}
		}
		mutex_unlock(&port_lock);
	}
	return rc;	
}

int32_t ptp_get_curr_ds(uint16_t const port, struct ptp_clock_identity *const clk_id, struct ptp_current_ds *const ds)
{
	int32_t rc = -1;
	struct ptp_port *ptr = get_ptp_port(port);
	memset(ds, 0, sizeof(struct ptp_current_ds));
	if(ptr != NULL)
	{
		rc = 0;
		mutex_lock(&port_lock);
		if (same_clock_id(&ptr->c->id, clk_id) || broadcast_clock_id(clk_id)) {
			struct ptp_clock *c = ptr->c;
			if(c != NULL)
			{
				rc = 0;
				memcpy(ds, &(c->cur), sizeof(struct ptp_current_ds));
			}
		}
		mutex_unlock(&port_lock);
	}
	return rc;	
}
