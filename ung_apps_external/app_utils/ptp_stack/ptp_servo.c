
#include <stdlib.h>
#include "ptp_servo.h"

/* Maximum hardware frequency adjustment is 6.25 ms. */
#define MAX_DRIFT_CORR                  6250000

#if 0
#define MAX_OFFSET_ADJ_ALLOWED          500
#else
#define MAX_OFFSET_ADJ_ALLOWED          5000
#endif

#if 0
#define MIN_FREQ_DIFF_ACCEPTED          50
#else
#define MIN_FREQ_DIFF_ACCEPTED          5000
#endif

#define MAX_FREQ_DIFF_ALLOWED           100000

/* Minimum interval for frequency update is 50 ms. */
#define MIN_FREQ_CORR_INTERVAL          50000000

/* Minimum interval for adjument is 125 ms. */
#define MIN_JUMP_CORR_INTERVAL          125000000

static int limit_drift(int *drift)
{
    int limit = 1;

    if (*drift < -MAX_DRIFT_CORR)
        *drift = -MAX_DRIFT_CORR;
    else if (*drift > MAX_DRIFT_CORR)
        *drift = MAX_DRIFT_CORR;
    else
        limit = 0;
    return limit;
}

void servo_adj_ingress(struct ptp_servo *s, struct ptu *offset)
{
    ptu_sub(&s->last_dbg_ingress, offset, &s->last_dbg_ingress);
    ptu_sub(&s->last_ingress, offset, &s->last_ingress);
    ptu_sub(&s->last_offset, offset, &s->last_offset);
}

int servo_sample(struct ptp_servo *s, struct ptu *offset,
    struct ptu *ingress, int *adj, int *state)
{
    struct ptu diff;
    struct ptu drift;
    struct ptu last_interval;
    s64 offset_nsec;
    s64 ki;
    s64 kp;
    s64 interval;
    s64 dbg_interval;
    int freq_diff;

    /* Very first Sync. */
    if (ptu_empty(&s->last_ingress)) {
        memcpy(&s->last_offset, offset, sizeof(struct ptu));
        memcpy(&s->last_ingress, ingress, sizeof(struct ptu));
        memcpy(&s->last_dbg_ingress, ingress, sizeof(struct ptu));
        *state = SERVO_UNLOCKED;
#ifdef DBG_PTP_SERVO
        ptu_str(offset, u1_str, sizeof(u1_str));
#endif
        return -1;
    }
    if (ptu_empty(&s->last_interval) && ptu_after(&s->last_ingress, ingress)) {
        ptu_zero(&s->last_ingress);
        *state = SERVO_UNLOCKED;
        return -1;
    }
    ptu_sub(ingress, &s->last_ingress, &last_interval);
    if (!ptu_empty(&s->last_interval)) {
#if 0
        ptu_sub(&last_interval, &s->last_interval, &diff);
        interval = ptu_get_nsec(&diff);
#else
        interval = ptu_get_nsec(&last_interval);
#endif
        if (interval >= (s64) 4 * NANOSEC_IN_SEC) {
            s->drift_valid = 0;
            ptu_zero(&s->last_interval);
            memcpy(&s->last_offset, offset, sizeof(struct ptu));
            memcpy(&s->last_ingress, ingress, sizeof(struct ptu));
            memcpy(&s->last_dbg_ingress, ingress, sizeof(struct ptu));
            *state = SERVO_UNLOCKED;
            return -1;
        }
    }
    memcpy(&s->last_interval, &last_interval, sizeof(struct ptu));

    /* Check for minimum interval. */
    interval = ptu_get_nsec(&s->last_interval);

    /* Ignore message if interval is too small. */
    if (interval < MIN_FREQ_CORR_INTERVAL)
        return -1;

    offset_nsec = ptu_get_nsec(offset);
    if (!s->drift_valid ||
        llabs(offset_nsec) >= MAX_OFFSET_ADJ_ALLOWED) {
        s64 freq;

        ptu_sub(offset, &s->last_offset, &diff);
        ptu_div_sec(&diff, &s->last_interval, &drift);
#ifdef DBG_PTP_SERVO
        ptu_str(&drift, u1_str, sizeof(u1_str));
#endif
        freq = ptu_get_nsec(&drift);
        freq *= 1000;
        if (abs(s->freq_diff) >= 100000) {
            freq = 0;
        }
        s->drift += freq;
        *adj = (int)(s->drift / 1000);
        if (limit_drift(adj))
            s->drift = *adj * 1000;
        *state = SERVO_LOCKING;
    }
    memcpy(&s->last_offset, offset, sizeof(struct ptu));
    memcpy(&s->last_ingress, ingress, sizeof(struct ptu));

    if (s->drift_valid &&
        llabs(offset_nsec) < MAX_OFFSET_ADJ_ALLOWED) {
        ki = s->ki * offset_nsec;
        kp = s->kp * offset_nsec;
        kp += ki + s->drift;
        if (kp >= 0)
            kp += 500;
        else
            kp -= 500;
        *adj = (int)(kp / 1000);
        if (!limit_drift(adj))
            s->drift += ki;
        if (llabs(offset_nsec) < MAX_OFFSET_ADJ_ALLOWED)
            *state = SERVO_LOCKED;
        else
            *state = SERVO_JUMP;
    }

    freq_diff = *adj - s->last_freq;
    if (!s->drift_valid &&
        (abs(freq_diff) < MIN_FREQ_DIFF_ACCEPTED ||
        interval > MIN_JUMP_CORR_INTERVAL)) {
        s->drift_valid = 1;
        *state = SERVO_JUMP;
    } else if (abs(freq_diff) >= MAX_FREQ_DIFF_ALLOWED) {
        *state = SERVO_JUMP_LONG;
    } else if (llabs(offset_nsec) >= MAX_OFFSET_ADJ_ALLOWED) {
        *state = SERVO_JUMP;
    }

    s->freq_diff = freq_diff;

    s->last_freq = *adj;
    return 0;
}

void init_ptp_servo(struct ptp_servo *s)
{
    s->ki = 100;
    s->kp = 500;
    ptu_zero(&s->last_interval);
    ptu_zero(&s->last_offset);
}
