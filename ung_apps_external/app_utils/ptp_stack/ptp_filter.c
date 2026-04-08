#include "ptp_filter.h"


static void mmedian_reset(struct mmedian *m)
{
    m->cnt = 0;
    m->index = 0;
}

static void mmedian_create(struct mmedian *m, int len)
{
    if (len < 1)
        return;

    m->data = mem_info_alloc(len * sizeof(struct mmedian_data));
    if (m->data)
        m->len = len;
}

static void mmedian_destroy(struct mmedian *m)
{
    mem_info_free(m->data);
    m->len = 0;
}

s64 filter_sample(struct mmedian *m, s64 delay)
{
    s64 median;
    int i;

    m->data[m->index].delays = delay;
    if (m->cnt < m->len) {
        m->cnt++;
    } else {
        for (i = 0; i < m->cnt; i++) {
            if (m->data[i].order == m->index)
                break;
        }
        for (; i + 1 < m->cnt; i++)
            m->data[i].order = m->data[i + 1].order;
    }
    for (i = m->cnt - 1; i > 0; i--) {
        if (m->data[m->data[i - 1].order].delays <= m->data[m->index].delays)
            break;
        m->data[i].order = m->data[i - 1].order;
    }
    m->data[i].order = m->index;
    m->index = (1 + m->index) % m->len;
    i = m->cnt / 2;
    if (m->cnt % 2) {
        median = m->data[m->data[i].order].delays;
    } else {
        median = m->data[m->data[i - 1].order].delays +
                 m->data[m->data[i].order].delays;
        median /= 2;
    }
    return median;
}  /* filter_sample */

void filter_set_clock_ratio(struct ptp_filter *f, double clock_ratio)
{
    f->clock_ratio = clock_ratio;
}

void filter_set_t1_t2(struct ptp_filter *f, struct ptu *t1, struct ptu *t2)
{
    memcpy(&f->t1, t1, sizeof(struct ptu));
    memcpy(&f->t2, t2, sizeof(struct ptu));
}

void filter_set_t3_t4(struct ptp_filter *f, struct ptu *t3, struct ptu *t4)
{
    memcpy(&f->t3, t3, sizeof(struct ptu));
    memcpy(&f->t4, t4, sizeof(struct ptu));
}

void filter_set_delay(struct ptp_filter *f, double delay)
{
    f->delay = delay;
    f->delay_valid = 1;
}

static s64 calc_delay(struct ptp_filter *f)
{
    struct ptu t23;
    struct ptu t41;
    struct ptu t1;
    struct ptu t2;
    s64 delay;

    ptu_sub(&f->t2, &f->t3, &t23);

    /* Only apply for 2-step Pdelay_Resp where the ratio can be calculated. */
    if (f->clock_ratio != 1.0) {
        ptu_mult(&t23, f->clock_ratio, &t23);
    }
    ptu_sub(&f->t4, &f->t1, &t41);
    ptu_add(&t23, &t41, &t1);
    ptu_div_int(&t1, 2, &t2);
    delay = ptu_get_nsec(&t2);

    /* Delay can be negative when the frequency difference is too great.
     * After the ratio is calculated it should return to normal.
     */
    /* Delay can be very big due to a hardware bug when 2-domain mode is
     * enabled and the transmit timestamp is wrong for clocks 1 and 2.
     * Pdelay_Req only fails the very first time right after 2-domain mode is
     * enabled.  Delay_Req always fails when sent right after receiving Sync.
     */
    if (delay < 0 || delay > 10000) {
        delay = 1;
    }
    return delay;
}

int filter_update_delay(struct ptp_filter *f, s64 *delay, s64 *raw)
{
    s64 raw_delay;

    if (ptu_empty(&f->t2) || ptu_empty(&f->t3))
        return -1;
    raw_delay = calc_delay(f);
    if (f->median.len)
        f->delay = filter_sample(&f->median, raw_delay);
    else
        f->delay = raw_delay;
    f->delay_valid = 1;
    if (delay)
        *delay = f->delay;
    if (raw)
        *raw = raw_delay;
    return 0;
}

int filter_update_offset(struct ptp_filter *f, struct ptu *offset)
{
    struct ptu t12;

    if (ptu_empty(&f->t1) || ptu_empty(&f->t2))
        return -1;
    ptu_sub(&f->t2, &f->t1, &t12);
    if (f->delay_valid) {
        struct ptu delay;

        delay.sec = 0;
        delay.nsec = f->delay << SCALED_NANOSEC_S;
        ptu_sub(&t12, &delay, offset);
    } else {
        memcpy(offset, &t12, sizeof(struct ptu));
    }
    return 0;
}

void filter_reset(struct ptp_filter *f, int full)
{
    ptu_zero(&f->t1);
    ptu_zero(&f->t2);
    ptu_zero(&f->t3);
    ptu_zero(&f->t4);
    if (full) {
        f->clock_ratio = 1.0;
        mmedian_reset(&f->median);
        f->delay_valid = 0;
    }
}

void exit_ptp_filter(struct ptp_filter *f)
{
    if (f->median.len) {
        mmedian_destroy(&f->median);
    }
}

void init_ptp_filter(struct ptp_filter *f)
{
    mmedian_create(&f->median, 10);
    f->clock_ratio = 1.0;
    f->delay_valid = 0;
}
