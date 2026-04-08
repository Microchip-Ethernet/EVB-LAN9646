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

#include <math.h>
#include <stdlib.h>
#include "ptp_servo.h"
#include "ptp_clock.h"

#if 0
#define DBG_PTP_DELAY
#endif
#if 0
#define DBG_PTP_RATIO
#endif
#if 0
#define DBG_PTP_SERVO
#endif

static void ptp_timestamp_to_scaled(struct ptp_timestamp *t,
    struct ptu *u)
{
    u64 s;

    s = t->sec.hi;
    s <<= 32;
    s |= t->sec.lo;
    u->sec = s;
    s = t->nsec;
    s <<= SCALED_NANOSEC_S;
    u->nsec = s;
}

static void ptp_utime_to_scaled(struct ptp_utime *t,
    struct ptu *u)
{
    u->sec = t->sec;
    u->nsec = t->nsec;
    u->nsec <<= SCALED_NANOSEC_S;
}

static void ptp_correction_to_scaled(s64 s, struct ptu *u)
{
    u->sec = s / SCALED_NANOSEC_IN_SEC;
    u->nsec = s % SCALED_NANOSEC_IN_SEC;
}

static void clock_set_freq(struct ptp_clock *c, int freq)
{
    void *fd = dev[1].fd;
    int rc;

    rc = adj_freq(fd, 0, 0, freq, NANOSEC_IN_SEC, c->index);
    (void) rc;
}

static void clock_adj_freq(struct ptp_clock *c, struct ptu *offset, int freq)
{
    void *fd = dev[1].fd;
    int sec;
    int nsec;
    int rc;

    sec = (int) offset->sec;
    nsec = (int)(offset->nsec >> SCALED_NANOSEC_S);
    rc = adj_freq(fd, -sec, -nsec, freq, NANOSEC_IN_SEC, c->index);
    if (!rc) {
        struct ptu u1, u2;

        servo_adj_ingress(&c->servo, offset);

        /* Need to update previously received timestamps. */
        if (c->last_sync) {
            ptp_utime_to_scaled(&c->last_sync->rx, &u1);
            ptu_sub(&u1, offset, &u2);
            c->last_sync->rx.sec = (int)u2.sec;
            c->last_sync->rx.nsec = (int)(u2.nsec >> SCALED_NANOSEC_S);
        }
        if (c->base_sync && c->base_sync != c->last_sync) {
            ptp_utime_to_scaled(&c->base_sync->rx, &u1);
            ptu_sub(&u1, offset, &u2);
            c->base_sync->rx.sec = (int)u2.sec;
            c->base_sync->rx.nsec = (int)(u2.nsec >> SCALED_NANOSEC_S);
        }
        if (c->base_sync)
            memcpy(&c->last_sync_rx, &c->base_sync->rx,
                   sizeof(struct ptp_utime));
    }
}

static void clock_adj(struct ptp_clock *c, struct ptu *offset)
{
    void *fd = dev[1].fd;
    int sec;
    int nsec;
    int rc;

    sec = (int) offset->sec;
    nsec = (int)(offset->nsec >> SCALED_NANOSEC_S);
    rc = adj_freq(fd, -sec, -nsec, 0, 0, c->index);
    if (!rc) {
        servo_adj_ingress(&c->servo, offset);
    }
}

static void port_set_latency(struct ptp_port *p)
{
    void *fd = dev[1].fd;

    set_delays(fd, p->index, p->rx_latency, p->tx_latency, p->asym_delay);
}

static void port_set_peer_delay(struct ptp_port *p)
{
    struct ptp_pdelay_info *info = &p->pdelay_info;
    void *fd = dev[1].fd;

    set_peer_delay(fd, p->index, (int) info->delay);
}

static void clock_path_delay(struct ptp_clock *c, struct ptu *tx,
    struct ptu *rx)
{
    struct ptp_port *h = clock_host_port(c);
    struct ptp_pdelay_info *info = &h->pdelay_info;
    s64 delay;

    filter_set_t3_t4(&info->filter, tx, rx);
    if (filter_update_delay(&info->filter, &info->delay, &info->raw_delay))
        return;

    delay = info->delay;
    delay <<= SCALED_NANOSEC_S;
    c->cur.meanPathDelay = delay;

    if (c->servo_state == SERVO_LOCKING) {
        if (filter_update_offset(&info->filter, &c->offset))
            return;
        clock_adj(c, &c->offset);
        c->servo_state = SERVO_JUMP;
        filter_reset(&info->filter, false);
    }
}  /* clock_path_delay */

static void clock_peer_delay(struct ptp_clock *c, struct ptu *tx,
    struct ptu *rx, struct ptp_pdelay_info *info)
{
    struct ptp_port *h = clock_host_port(c);
    struct ptp_pdelay_info *pdelay = &h->pdelay_info;
    s64 delay = info->delay;

    pdelay->delay = info->delay;
    pdelay->raw_delay = info->raw_delay;
    pdelay->nrate.ratio = info->nrate.ratio;
    filter_set_delay(&pdelay->filter, info->delay);

    delay <<= SCALED_NANOSEC_S;
    c->cur.meanPathDelay = delay;
}  /* clock_peer_delay */

#ifdef DBG_PTP_SERVO
static void display_servo(struct ptp_clock *c, int adj)
{
    struct ptp_servo *s = &c->servo;
    struct ptu diff;
    s64 interval;
    s64 dbg_interval;
    s64 offset_nsec;
    struct ptp_port *h = clock_host_port(c);
    struct ptp_pdelay_info *info = &h->pdelay_info;

    interval = ptu_get_nsec(&s->last_interval);
    offset_nsec = ptu_get_nsec(&c->offset);
    ptu_sub(&s->last_ingress, &s->last_dbg_ingress, &diff);
    dbg_interval = ptu_get_nsec(&diff);
    if (dbg_interval >= NANOSEC_IN_SEC - 5000000) {
        if (h->cnt_sync_rx < 200)
            h->cnt_sync_rx--;
        memcpy(&s->last_dbg_ingress, &s->last_ingress, sizeof(struct ptu));
        if (llabs(offset_nsec) >= (s64) 2 * NANOSEC_IN_SEC) {
            ptu_str(&s->last_offset, u1_str, sizeof(u1_str));
            s64_str(offset_nsec, u2_str, sizeof(u2_str));
            offset_nsec /= NANOSEC_IN_SEC;
        }
        if (interval >= (s64) 2 * NANOSEC_IN_SEC)
            interval /= NANOSEC_IN_SEC;
#ifdef DEBUG_MSG
#ifdef LINUX_PTP
dbg_msg("%d=<%d %d> [%d] (%d %d) %d"NL, c->servo_state, adj, s->freq_diff,
#else
printf("%d=<%d %d> [%d] (%d %d) %d"NL, c->servo_state, adj, s->freq_diff,
#endif
(int) offset_nsec, (int) info->delay, (int) info->raw_delay, (int) interval);
#endif
    }
}  /* display_servo */
#endif

static int clock_sync(struct ptp_clock *c, struct ptu *ingress,
    struct ptu *origin)
{
    int state = c->servo_state;
    s64 offset;
    int adj;
    struct ptp_port *h = clock_host_port(c);
    struct ptp_pdelay_info *info = &h->pdelay_info;

    filter_set_t1_t2(&info->filter, origin, ingress);
    if (filter_update_offset(&info->filter, &c->offset))
        return state;

    offset = ptu_get_nsec(&c->offset);
    offset <<= SCALED_NANOSEC_S;
    c->cur.offsetFromMaster = offset;

    if (servo_sample(&c->servo, &c->offset, ingress, &adj, &state))
        return state;
    if (state == SERVO_JUMP) {
        if (!info->filter.delay_valid) {
            state = SERVO_LOCKING;
        }
    }

    switch (state) {
    case SERVO_JUMP_LONG:
    case SERVO_JUMP:
        clock_adj_freq(c, &c->offset, adj);
        filter_reset(&info->filter, true);
        if (c->delay_mechanism == DELAY_NONE)
            filter_set_delay(&info->filter, 0);
        break;
    case SERVO_LOCKING:
        clock_set_freq(c, adj);
        break;
    case SERVO_LOCKED:
        clock_set_freq(c, adj);
        break;
    default:
        break;
    }
    c->servo_state = state;
#ifdef DBG_PTP_SERVO
    if (h->cnt_sync_rx) {
        display_servo(c, adj);
    }
#endif
    return state;
}  /* clock_sync */

void port_sync(struct ptp_port *p, struct ptp_utime *ingress,
    struct ptp_timestamp *origin, s64 cor_sync, s64 cor_fup, u16 seqid)
{
    struct ptp_pdelay_info *info = &p->pdelay_info;
    struct ptp_clock *c = p->c;
    struct ptu t1;
    struct ptu t2;
    struct ptu c1;
    struct ptu c2;
    struct ptu c12;
    struct ptu t1c;
    int state;

    if (p->multi_pdelay)
        info = p->multi_pdelay->pdelay_info;
    ptp_timestamp_to_scaled(origin, &t1);
    ptp_utime_to_scaled(ingress, &t2);
    ptp_correction_to_scaled(cor_sync, &c1);
    ptu_zero(&c2);
    if (cor_fup)
        ptp_correction_to_scaled(cor_fup, &c2);
    ptu_add(&c1, &c2, &c12);
    ptu_add(&t1, &c12, &t1c);

    state = clock_sync(c, &t2, &t1c);
    switch (state) {
    case SERVO_UNLOCKED:
        break;
    case SERVO_JUMP_LONG:
        if (p->gptp)
            ptu_empty(&info->nrate.rx2);
        else
            filter_reset(&info->filter, 1);
        if (!p->gptp_auto)
            port_set_state(p, port_event(p, EVENT_CLOCK_UNSTABLE));
        break;
    case SERVO_JUMP:
        if (!p->gptp_auto)
            port_set_state(p, port_event(p, EVENT_CLOCK_UNSTABLE));
        break;
    case SERVO_LOCKING:
        break;
    case SERVO_LOCKED:
        if (p->state != PTP_S_SLAVE)
            port_set_state(p, port_event(p, EVENT_CLOCK_STABLE));
        break;
    default:
        break;
    }
    do {
        uint sec = 0, nsec = 0;
        int n;

        if (!c->first_sync) {
            n = get_clock(dev[1].fd, &sec, &nsec, 0);
            c->first_sync = 1;
            exception_log(sec, nsec, "Received Sync %04x at p-%u",
                          seqid, p->id);
            c->report_sync = true;
        }
        if (c->report_sync) {
            if (automotive_test_mode && p->report_ready_done) {
                struct aed_test_status *msg = (struct aed_test_status *)
                    &eth_avtpdu_buf[14];
                u64 timestamp;

                if (!sec && !nsec)
                    n = get_clock(dev[1].fd, &sec, &nsec, 0);
                timestamp = sec;
                timestamp *= 1000000000;
                timestamp += nsec;
                update_aed_test_status(msg, avtpdu_seqid++,
                    AED_DESC_TYPE_AVB_INTERFACE, p->index - 1, timestamp,
                    AED_STATE_AVB_SYNC);
                send_aed(msg, 1 << (p->index - 1));
                c->report_sync = false;
#ifdef LINUX_PTP
dbg_msg("send avb\n");
#endif
            } else if (!automotive_test_mode) {
                c->report_sync = false;
            }
        }
    } while (0);
}  /* port_sync */

void port_path_delay(struct ptp_port *p, struct ptp_utime *egress,
    struct ptp_timestamp *receive, s64 corr_resp)
{
    struct ptu t3;
    struct ptu t4;
    struct ptu c3;
    struct ptu t4c;

    ptp_utime_to_scaled(egress, &t3);
    ptp_timestamp_to_scaled(receive, &t4);
    ptp_correction_to_scaled(corr_resp, &c3);
    ptu_sub(&t4, &c3, &t4c);

    clock_path_delay(p->c, &t3, &t4c);
}  /* port_path_delay */

static void port_calc_nrate(struct ptp_port *p, struct ptp_pdelay_info *info,
    struct ptu *tx, struct ptu *rx)
{
    struct nrate_info *n = &info->nrate;
    struct ptu own_interval;
    struct ptu other_interval;
    struct ptu diff;
    s64 diff_nsec;

    if (!n->rx1.sec) {
        memcpy(&n->rx1, rx, sizeof(struct ptu));
        memcpy(&n->tx1, tx, sizeof(struct ptu));
        memcpy(&n->rx2, rx, sizeof(struct ptu));
        memcpy(&n->tx2, tx, sizeof(struct ptu));
        return;
    }
    ptu_sub(rx, &n->rx2, &own_interval);
    ptu_sub(tx, &n->tx2, &other_interval);
    ptu_sub(&own_interval, &other_interval, &diff);
    diff_nsec = ptu_get_nsec(&diff);
    n->last_diff = diff_nsec;
    memcpy(&n->rx2, rx, sizeof(struct ptu));
    memcpy(&n->tx2, tx, sizeof(struct ptu));

    /* Make sure the interval is not too big. */
    if (llabs(diff_nsec) > NANOSEC_IN_SEC / 16) {
        memcpy(&n->rx1, rx, sizeof(struct ptu));
        memcpy(&n->tx1, tx, sizeof(struct ptu));
        n->ratio = 1.0;
        return;
    }
    ptu_sub(tx, &n->tx1, &other_interval);
    ptu_sub(rx, &n->rx1, &own_interval);
    n->ratio = ptu_div_ratio(&other_interval, &own_interval);
    n->last_ratio = n->ratio;
    memcpy(&n->rx1, rx, sizeof(struct ptu));
    memcpy(&n->tx1, tx, sizeof(struct ptu));
    n->ratio_valid = 1;
}

void port_assign_peer_delay(struct ptp_port *p, struct ptp_pdelay_info *info)
{
    if (p->state == PTP_S_UNCALIBRATED || p->state == PTP_S_SLAVE) {
        clock_peer_delay(p->c, NULL, NULL, info);
    }
    p->ds.peerMeanPathDelay = info->delay << SCALED_NANOSEC_S;
}

void port_peer_delay(struct ptp_port *p, struct ptp_pdelay_info *info,
    struct ptp_utime *egress, struct ptp_utime *ingress,
    struct ptp_timestamp *receive, s64 corr_resp,
    struct ptp_timestamp *transmit, s64 corr_fup)
{
    struct ptp_pdelay_info *first_info = &p->pdelay_info;
    struct ptu c1;
    struct ptu c2;
    struct ptu c12;
    struct ptu c3;
    struct ptu t1;
    struct ptu t2;
    struct ptu t3;
    struct ptu t4;
    struct ptu t3c;
    u8 src = 0;

    ptp_utime_to_scaled(egress, &t1);
    ptp_utime_to_scaled(ingress, &t4);
    ptp_correction_to_scaled(corr_resp, &c1);
    if (!transmit) {
        ptu_zero(&t2);
        ptu_zero(&t3);
        ptu_zero(&c2);
        memcpy(&c3, &t1, sizeof(struct ptu));
    } else {
        ptp_timestamp_to_scaled(receive, &t2);
        ptp_timestamp_to_scaled(transmit, &t3);
        ptp_correction_to_scaled(corr_fup, &c2);
        ptu_zero(&c3);
    }
    ptu_add(&c1, &c2, &c12);
    ptu_add(&t3, &c12, &t3c);

    ptu_add(&t3c, &c3, &c12);
    port_calc_nrate(p, info, &c12, &t4);
    filter_set_clock_ratio(&info->filter, info->nrate.ratio);
    filter_set_t3_t4(&info->filter, &t1, &t2);
    filter_set_t1_t2(&info->filter, &t3c, &t4);
    if (filter_update_delay(&info->filter, &info->delay, &info->raw_delay))
        return;
    if (p->state == PTP_S_UNCALIBRATED || p->state == PTP_S_SLAVE) {
        clock_peer_delay(p->c, &t1, &t2, info);
    }
    if (p->multi_pdelay)
        first_info = p->multi_pdelay->pdelay_info;
    if (info != first_info)
        return;
    p->ds.peerMeanPathDelay = info->delay << SCALED_NANOSEC_S;
    port_set_peer_delay(p);
    port_check_capable(p);
}  /* port_peer_delay */
