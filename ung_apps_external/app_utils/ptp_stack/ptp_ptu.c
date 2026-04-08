
#include <stdlib.h>
#include "ptp_ptu.h"


int ptu_str(struct ptu *u, char *str, size_t size)
{
    s64 nsec = u->nsec;
    uint sub;
    int len;
    int neg = 0;

    if (nsec < 0) {
        neg = 1;
        nsec = -nsec;
    }
    sub = nsec & ((1 << SCALED_NANOSEC_S) - 1);
    nsec >>= SCALED_NANOSEC_S;
    if (!u->sec && neg)
        len = snprintf(str, size, "-0.%09u.%04x",
            (int) nsec, sub);
    else
        len = snprintf(str, size, "%d.%09u.%04x",
            (int) u->sec, (int) nsec, sub);
    return len;
}

int s64_str(s64 n, char *str, size_t size)
{
    s64 sec;
    s64 nsec;
    int len;

    sec = n / NANOSEC_IN_SEC;
    nsec = n % NANOSEC_IN_SEC;
    if (sec < 0)
        nsec = -nsec;
    if (!sec && nsec < 0)
        len = snprintf(str, size, "-0.%09d", (int) -nsec);
    else
        len = snprintf(str, size, "%d.%09u", (int) sec, (int) nsec);
    return len;
}

int float_str(double d, char *str, size_t size)
{
    int len;
    s64 n;
    s64 f;

    d *= 1000000;
    n = d;
    f = n % 1000000;
    n = n / 1000000;
    if (n < 0)
        f = -f;
    if (!n && f < 0)
        len = snprintf(str, size, "-0.%06d", (int) -f);
    else
        len = snprintf(str, size, "%d.%06u", (int) n, (int) f);
    return len;
}

int timestamp_str(u32 lsec, u32 nsec, char *str, size_t size)
{
    u32 day, hour, min, sec;
    int len;

    sec = lsec % 60;
    min = (lsec - sec) / 60;
    hour = min / 60;
    min = min % 60;
    day = hour / 24;
    hour = hour % 24;
    if (day)
        len = snprintf(str, size, "%u.%02u:%02u:%02u.%09u", day, hour, min,
            sec, nsec);
    else if (hour)
        len = snprintf(str, size, "%u:%02u:%02u.%09u", hour, min, sec, nsec);
    else if (min)
        len = snprintf(str, size, "%u:%02u.%09u", min, sec, nsec);
    else
        len = snprintf(str, size, "%u.%09u", sec, nsec);
    return len;
}

int ptu_after(struct ptu *u1, struct ptu *u2)
{
    return ((u1->sec > u2->sec) ||
            (u1->sec == u2->sec && u1->nsec > u2->nsec));
}

int ptu_empty(struct ptu *u)
{
    return (!u->sec && !u->nsec);
}

void ptu_zero(struct ptu *u)
{
    memset(u, 0, sizeof(*u));
}

s64 ptu_get_nsec(struct ptu *u)
{
    s64 nsec = 0;

    if (u->sec) {
        nsec = u->sec;
        nsec *= NANOSEC_IN_SEC;
    }
    nsec += u->nsec >> SCALED_NANOSEC_S;
    return nsec;
}

void ptu_add(struct ptu *u1, struct ptu *u2, struct ptu *u3)
{
    s64 sec;
    s64 nsec;

    sec = u1->sec + u2->sec;
    nsec = u1->nsec + u2->nsec;
    if (nsec >= SCALED_NANOSEC_IN_SEC) {
        nsec -= SCALED_NANOSEC_IN_SEC;
        sec++;
    } else if (nsec <= -SCALED_NANOSEC_IN_SEC) {
        nsec += SCALED_NANOSEC_IN_SEC;
        sec--;
    }
    if (sec > 0 && nsec < 0) {
        nsec += SCALED_NANOSEC_IN_SEC;
        sec--;
    } else if (sec < 0 && nsec > 0) {
        nsec = -(SCALED_NANOSEC_IN_SEC - nsec);
        sec++;
    }
    u3->sec = sec;
    u3->nsec = nsec;
}

void ptu_sub(struct ptu *u1, struct ptu *u2, struct ptu *u3)
{
    struct ptu u4;

    memcpy(&u4, u2, sizeof(u4));
    u4.sec = -u4.sec;
    u4.nsec = -u4.nsec;
    ptu_add(u1, &u4, u3);
}

void ptu_div_int(struct ptu *u1, s64 divisor, struct ptu *u3)
{
    s64 nsec = u1->sec;

    nsec %= divisor;
    nsec *= NANOSEC_IN_SEC;
    nsec <<= SCALED_NANOSEC_S;
    nsec /= divisor;
    u3->sec = u1->sec / divisor;
    u3->nsec = u1->nsec / divisor;
    u3->nsec += nsec;
}

void ptu_div_sec(struct ptu *u1, struct ptu *u2, struct ptu *u3)
{
    s64 nsec1;
    s64 nsec2;

    nsec1 = ptu_get_nsec(u1);
    nsec2 = ptu_get_nsec(u2);
    nsec1 *= NANOSEC_IN_SEC;
    nsec1 /= nsec2;
    u3->sec = nsec1 / NANOSEC_IN_SEC;
    u3->nsec = nsec1 % NANOSEC_IN_SEC;
    u3->nsec <<= SCALED_NANOSEC_S;
}

double ptu_div_ratio(struct ptu *u1, struct ptu *u2)
{
    double d1;
    double d2;
    double r;

    d1 = ptu_get_nsec(u1);
    d2 = ptu_get_nsec(u2);
    r = d1 / d2;
    return r;
}

void ptu_mult(struct ptu *u1, double r, struct ptu *u3)
{
    double d;
    s64 s;

    d = u1->sec;
    d *= NANOSEC_IN_SEC;
    d += u1->nsec;
    d *= r;
    s = d;
    u3->sec = s / SCALED_NANOSEC_IN_SEC;
    u3->nsec = s % SCALED_NANOSEC_IN_SEC;
}

#define POW2_41  ((double)((s64)1 << 41))

int float_to_ratio(double ratio)
{
    ratio *= POW2_41;
    ratio -= POW2_41;
    return (int)ratio;
}

double ratio_to_float(int ratio)
{
    double r = ratio;

    r += POW2_41;
    if (r < 0)
        r -= (POW2_41 / 10000000);
    else
        r += (POW2_41 / 10000000);
    r /= POW2_41;
    return r;
}
