
#include <string.h>
#include "ptp_bmca.h"
#include "ptp_port.h"
#include "ptp_clock.h"
#include "ptp_protocol.h"


#define A_BETTER_PATH   2
#define A_BETTER        1
#define B_BETTER        -1
#define B_BETTER_PATH   -2

/* This algorithm does not apply to Transparent Clock as the conditions are
 * all fixed.  RSTP is supposed to be running to eliminate redundant messages.
 * The gPTP profile uses special multicast MAC address and so runs its own
 * looping prevention.
 */
static int bmca_better_path(struct bmc_dataset *a, struct bmc_dataset *b)
{
    int diff;
    uint steps_a = a->steps_removed;
    uint steps_b = b->steps_removed;

    /* Fewer steps and shorter path is better. */
    if (steps_a + 1 < steps_b)
        return A_BETTER;
    if (steps_b + 1 < steps_a)
        return B_BETTER;

    if (steps_a < steps_b) {
        diff = memcmp(&b->rx, &b->tx, sizeof(struct ptp_port_identity));
        if (diff < 0)
            return A_BETTER;
        if (diff > 0)
            return A_BETTER_PATH;
        return 0;
    }
    if (steps_b < steps_a) {
        diff = memcmp(&a->rx, &a->tx, sizeof(struct ptp_port_identity));
        if (diff < 0)
            return B_BETTER;
        if (diff > 0)
            return B_BETTER_PATH;
        return 0;
    }

    diff = memcmp(&a->tx, &b->tx, sizeof(struct ptp_port_identity));
    if (diff < 0)
        return A_BETTER_PATH;
    if (diff > 0)
        return B_BETTER_PATH;

    if (a->rx.port < b->rx.port)
        return A_BETTER_PATH;
    if (b->rx.port < a->rx.port)
        return B_BETTER_PATH;

    return 0;
}  /* bmca_better_path */

int bmca_better(struct bmc_dataset *a, struct bmc_dataset *b)
{
    int diff;

    /* The dataset is the same. */
    if (a == b)
        return 0;

    /* Only one is compared. */
    if (a && !b)
        return A_BETTER;
    if (b && !a)
        return B_BETTER;

    /* Same clock compared.  Check which path is better. */
    diff = memcmp(&a->id, &b->id, sizeof(struct ptp_clock_identity));
    if (!diff)
        return bmca_better_path(a, b);

    if (a->prio_1 < b->prio_1)
        return A_BETTER;
    if (b->prio_1 < a->prio_1)
        return B_BETTER;

    if (a->quality.clockClass < b->quality.clockClass)
        return A_BETTER;
    if (b->quality.clockClass < a->quality.clockClass)
        return B_BETTER;

    if (a->quality.clockAccuracy < b->quality.clockAccuracy)
        return A_BETTER;
    if (b->quality.clockAccuracy < a->quality.clockAccuracy)
        return B_BETTER;

    /* Note 0xFFFF means unknown, but compliant test increases it by one so it
     * becomes 0 and messes up the testing.  So the default value cannot be
     * 0xFFFF.
     */
    if (a->quality.offsetScaledLogVariance < b->quality.offsetScaledLogVariance)
        return A_BETTER;
    if (b->quality.offsetScaledLogVariance < a->quality.offsetScaledLogVariance)
        return B_BETTER;

    if (a->prio_2 < b->prio_2)
        return A_BETTER;
    if (b->prio_2 < a->prio_2)
        return B_BETTER;

    /* Clock id is the final determinator. */
    return diff < 0 ? A_BETTER : B_BETTER;
}  /* bmca_better */

static bool port_can_accept_announce(struct ptp_clock *c,
                                     struct bmc_dataset *own,
                                     struct bmc_dataset *best)
{
    if (clock_class(c) <= 127 ||
        (best &&
         !memcmp(&own->id, &best->id, sizeof(struct ptp_clock_identity))))
        return false;
    return true;
}

int bmca_decision(struct ptp_clock *c, struct ptp_port *p)
{
    struct bmc_dataset *clock_own;
    struct bmc_dataset *clock_best;
    struct bmc_dataset *port_best;
    int state;
    struct ptp_port *h = &c->host_port;

    clock_own = clock_default_ds(c);
    clock_best = clock_best_ds(c);
    port_best = port_best_ds(p);
    state = port_state(p);
    clock_own->tx.port = p->index;

    if (!port_best &&
        (PTP_S_LISTENING == state || PTP_S_FAULTY == state)) {
        return state;
    }

    /* Cannot be a slave clock. */
    if (!port_can_accept_announce(c, clock_own, port_best)) {
        if (bmca_better(clock_own, port_best) > 0)
            return PTP_S_MASTER;
        else
            return PTP_S_PASSIVE;
    }

    /* clock_best will be NULL if own clock is best. */
    if (clock_class(c) != 255 && bmca_better(clock_own, clock_best) > 0)
        return PTP_S_MASTER;

    /* Only one will be a slave port. */
    if (clock_best_port(c) == p)
        return PTP_S_SLAVE;
    else if (!clock_best_port(c)) {
        if (PTP_S_FAULTY == state)
            return state;
        return PTP_S_LISTENING;
    }

    /* Other ports will be either passive or master. */
    if (bmca_better(clock_best, port_best) == A_BETTER_PATH)
        return PTP_S_PASSIVE;
    else
        return PTP_S_PRE_MASTER;
}  /* bmca_decision */

struct foreign_dataset *find_best_master(struct foreign_dataset masters[])
{
    struct foreign_dataset *f;
    struct foreign_dataset *best = NULL;
    int i;

    for (i = 0; i < MAX_FOREIGN_MASTERS; i++) {
        f = &masters[i];
        if (!f->valid)
            continue;
        if (!best)
            best = f;
        else if (bmca_better(&f->dataset, &best->dataset) > 0)
            best = f;
    }
    return best;
}  /* find_best_master */

struct foreign_dataset *find_foreign_master(struct foreign_dataset masters[],
    struct bmc_dataset *ds)
{
    struct foreign_dataset *f;
    struct foreign_dataset *first = NULL;
    int i;

    for (i = 0; i < MAX_FOREIGN_MASTERS; i++) {
        f = &masters[i];
        if (!first && !f->cnt)
            first = f;
        if (f->cnt && same_port_id(&ds->tx, &f->dataset.tx))
            return f;
    }
    if (first) {
        f = first;
        memcpy(&f->dataset, ds, sizeof(struct bmc_dataset));
    }
    return first;
}  /* find_foreign_master */