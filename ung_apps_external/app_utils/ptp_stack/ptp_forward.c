

#include "ptp_protocol.h"
#include "ptp_forward.h"


/* 8 hours */
#define MAX_ALLOWED_CORRECTION_SEC  28800

void ptp_check_residence(struct ptp_clock *c)
{
    s64 residence = c->residence;
    s64 residence_sec;

    /* Update last received Sync/Follow_Up when they are not forwarded. */
    if (c->last_sync != c->base_sync) {
        msg_save(&c->last_sync, c->base_sync);
    }
    if (c->last_fup != c->base_fup) {
        memcpy(&c->last_sync_rx, &c->base_sync->rx, sizeof(struct ptp_utime));
        msg_save(&c->last_fup, c->base_fup);
        msg_save(&c->last_rx_fup, c->base_fup);
    }

    /* Check if the second is over the limit. */
    residence += 120000000;
    residence_sec = residence / NANOSEC_IN_SEC;

    /* Correction can become too big. */
    if (residence_sec >= MAX_ALLOWED_CORRECTION_SEC) {
        struct ptp_msg *msg;

        c->last_sync_rx.sec += MAX_ALLOWED_CORRECTION_SEC;
        msg = c->last_sync->msg;
        if (msg->data.sync.originTimestamp.sec.lo)
            msg->data.sync.originTimestamp.sec.lo +=
                MAX_ALLOWED_CORRECTION_SEC;
        msg = c->last_fup->msg;
        msg->data.follow_up.preciseOriginTimestamp.sec.lo +=
            MAX_ALLOWED_CORRECTION_SEC;

        /* Clear in case it is never updated again after this call. */
        c->residence = 0;
    }
    c->last_sync->msg->hdr.sequenceId++;
    c->last_fup->msg->hdr.sequenceId++;
}  /* ptp_check_residence */

void ptp_fwd_fup(struct ptp_port *p, struct ptp_message *m)
{
    struct ptp_clock *c = p->c;
    struct ptp_msg_info *sync = &p->tx_sync;
    struct ptp_msg *msg;
    s64 rx;
    s64 tx;
    s64 residence;
    int first = 0;

    /* This is the first forwarded Sync. */
    if (!c->first_sync_tx.sec) {
        first = 1;
        memcpy(&c->first_sync_tx, &sync->hw_time, sizeof(struct ptp_utime));

        /* No real received Sync.  One was simulated. */
        if (!c->last_sync_rx.sec && !c->last_sync_rx.nsec) {
            msg = c->last_fup->msg;
            memcpy(&c->last_sync_rx, &sync->hw_time, sizeof(struct ptp_utime));
            ptp_utime_to_timestamp(0, &sync->hw_time,
                &msg->data.follow_up.preciseOriginTimestamp);
        }
    }
    rx = c->last_sync_rx.sec;
    rx *= 1000000000;
    rx += c->last_sync_rx.nsec;
    tx = sync->hw_time.sec;
    tx *= 1000000000;
    tx += sync->hw_time.nsec;
    if (tx < rx) {
        return;
    }
    residence = tx - rx;
    rx = c->cur.meanPathDelay;
    rx >>= SCALED_NANOSEC_S;
    residence += rx;
    if (first)
        c->residence = residence;

    residence <<= SCALED_NANOSEC_S;

    port_fwd_fup(p, c->last_fup, residence);
}  /* ptp_fwd_fup */

void ptp_save_sync(struct ptp_clock *c, struct ptp_message *m)
{
    msg_save(&c->base_sync, m);
}

void ptp_save_fup(struct ptp_clock *c, struct ptp_message *m)
{
    msg_save(&c->base_fup, m);
}

void ptp_forward(struct ptp_port *q, struct ptp_message *m)
{
    struct ptp_clock *c = q->c;
    struct ptp_port *h = clock_host_port(c);
    int n;
    struct ptp_port *p;
    struct ptp_msg *msg = m->msg;

    if (msg->hdr.messageType == SYNC_MSG) {
        c->first_sync_tx.sec = 0;

        /* Clear last Follow_Up. */
        msg_clear(&c->last_fup);

        /* Remember this Sync. */
        msg_save(&c->last_sync, m);
    } else if (msg->hdr.messageType == FOLLOW_UP_MSG) {

        /* Can only use the last Sync receive time if Follow_Up is received. */
        if (c->last_sync)
            memcpy(&c->last_sync_rx, &c->last_sync->rx,
                sizeof(struct ptp_utime));

        /* Remember this Follow_Up. */
        msg_save(&c->last_fup, m);
        msg_save(&c->last_rx_fup, m);
    } else if (msg->hdr.messageType == ANNOUNCE_MSG) {

        /* Reset the count so Announce is not generated. */
        c->ann_cnt = 0;

        /* Remember this Announce. */
        msg_save(&c->last_ann, m);
    }
    for (n = 1; n <= c->cnt; n++) {
        p = c->ptp_ports[n];

        /* Skip the slave port. */
        if (p->state != PTP_S_MASTER)
            continue;

        /* Forward only when asCapable. */
        if (!port_capable(p))
            continue;

        tx_msg_lock();
        switch (msg->hdr.messageType) {
        case SYNC_MSG:
            port_fwd_sync(p, m);
            break;
        case FOLLOW_UP_MSG:

            /* Sync timestamp is available. */
            if (p->tx_sync.hw_time.sec)
                ptp_fwd_fup(p, m);
            break;
        case ANNOUNCE_MSG:
            if (!c->no_announce)
                port_fwd_announce(p, m);
            break;
        }
        tx_msg_unlock();
    }
}  /* ptp_forward */

void ptp_setup_sync_interval(struct ptp_clock *c)
{
    struct ptp_port *h;
    struct ptp_port *p;
    int interval;
    int diff;
    int n;

    h = clock_host_port(c);
    interval = h->logSyncInterval;
#if 1
    interval = c->logSyncInterval;
#endif

    /* Update the Sync interval count. */
    for (n = 1; n <= c->cnt; n++) {
        p = c->ptp_ports[n];
        if (p->state == PTP_S_SLAVE || p->state == PTP_S_UNCALIBRATED)
            continue;
        diff = p->logSyncInterval - interval;

        /* Port is not connected and has smaller interval. */
        if (diff < 0)
            diff = 0;
        if (p->max_sync_tx != (1 << diff)) {
            p->max_sync_tx = (1 << diff);
            p->cnt_sync_tx = p->max_sync_tx;
            p->cnt_fup_tx = p->max_sync_tx;
        }
    }
}  /* ptp_setup_sync_interval */

static int ptp_get_fastest_interval(struct ptp_clock *c, int interval)
{
    struct ptp_port *p;
    int n;

    for (n = 1; n <= c->cnt; n++) {
        p = c->ptp_ports[n];

        /* Ignore port not actually running. */
#if 0
        if (p->state != PTP_S_MASTER && p->state != PTP_S_SLAVE)
#else
        if (p->state != PTP_S_MASTER)
#endif
            continue;
        if (p->logSyncInterval < interval)
            interval = p->logSyncInterval;
    }
    return interval;
}

void ptp_set_sync_interval(struct ptp_clock *c)
{
    int force = c->set_interval_force;
    struct ptp_port *h;
    struct ptp_port *p;
    int interval;
    int diff;
    int n;

    h = clock_host_port(c);
    if (!h->gptp_auto)
        return;
    interval = h->logSyncInterval;
    p = clock_slave_port(c);
    if (p)
        interval = p->logSyncInterval;
    else
        interval = 1;
    interval = ptp_get_fastest_interval(c, interval);
#if 0
dbg_msg("set_sync: %d %d %d %lu\n", force, interval, c->signaling, jiffies);
#endif
    if (interval != h->logSyncInterval) {
        bool slower = h->logSyncInterval < interval;

        h->logSyncInterval = interval;
        if (h->state == PTP_S_MASTER || c->sync_timeout) {
            clock_change_sync_timer(c, interval);
        } else if (!c->req_interval) {
            p = clock_slave_port(c);
            if (p && p->state == PTP_S_SLAVE) {
                struct ptp_message *m = NULL;

                /* Receive Sync rate is slower so stop the receive timer. */
                if (slower) {
                    clock_stop_sync_interval(c);
                    clock_stop_sync_timeout(c);
                }
                m = alloc_msg(NULL, 100, NULL);
                if (m) {
                    tx_msg_lock();
                    port_tx_signal_interval(p, 127, interval, 127, m);
                    tx_msg_unlock();
#ifdef NOTIFY_PTP_EVENT
                    exception_log(0, 0, "Sync interval changes to %d at p-%u",
                        interval, p->id);
#endif
                }
                force = 0;

#if 0
                /* Timer is 1 second so no need to send request again in case
                 * of miss.
                 */
                if (interval < 0)
#endif
                    clock_start_signaling_timer(c, false);
            }
        }
    }
    if (force) {
        struct ptp_message *m = NULL;

        p = clock_slave_port(c);
        if (p && p->state == PTP_S_SLAVE) {
            m = alloc_msg(NULL, 100, NULL);
            if (m) {
                tx_msg_lock();
                port_tx_signal_interval(p, 127, interval, 127, m);
                tx_msg_unlock();
#ifdef NOTIFY_PTP_EVENT
                exception_log(0, 0, "Sync interval changes to %d at p-%u",
                    interval, p->id);
#endif
            }
        }
    }
    ptp_setup_sync_interval(c);
}

void ptp_change_master_interval(struct ptp_port *p, int interval)
{
    if (p->state != PTP_S_MASTER)
        return;
    if (p->logSyncInterval != interval) {
        p->logSyncInterval = interval;
        p->ds.logSyncInterval = interval;

        /* In case all ports are changing the interval at the same time. */
        p->c->set_interval_force = 0;
        schedule_delayed_work(&p->c->update_interval_work, msecs_to_jiffies(10));
#ifdef NOTIFY_PTP_EVENT
        exception_log(0, 0, "Sync interval is requested for %d at p-%u",
            p->logSyncInterval, p->id);
#endif
    }
}

void ptp_change_slave_interval(struct ptp_clock *c, int interval, bool now)
{
    struct ptp_port *h = clock_host_port(c);

#if 0
dbg_msg("change_slave: %d %d %lu\n", h->logSyncInterval, interval, jiffies);
#endif
    interval = ptp_get_fastest_interval(c, interval);
    if (h->logSyncInterval != interval || now) {
        struct ptp_port *p = clock_slave_port(c);

        if (!p)
            return;

        /* Receive Sync rate will be slower so stop the receive timer. */
        if (h->logSyncInterval < interval) {
            clock_stop_sync_interval(c);
            clock_stop_sync_timeout(c);
        }
        h->logSyncInterval = interval;
        tx_msg_lock();
        port_tx_signal_interval(p, 127, h->logSyncInterval, 127, NULL);
        tx_msg_unlock();
#ifdef NOTIFY_PTP_EVENT
        exception_log(0, 0, "Sync interval changes to %d at p-%u",
            h->logSyncInterval, p->id);
#endif

#if 0
        /* Timer is 1 second so no need to send request again in case of
         * miss.
         */
        if (h->logSyncInterval < 0)
#endif
            clock_start_signaling_timer(c, false);
    }
    ptp_setup_sync_interval(c);
}
