
#ifndef PTP_BMCA_H
#define PTP_BMCA_H

#define FOREIGN_MASTER_THRESHOLD  3

#define MAX_FOREIGN_MASTERS  2

struct bmc_dataset {
    u8 prio_1;
    u8 prio_2;
    u16 steps_removed;
    struct ptp_clock_identity id;
    struct ptp_clock_quality quality;
    struct ptp_port_identity rx;
    struct ptp_port_identity tx;
};

struct foreign_dataset {
    int prec;
    struct bmc_dataset dataset;
    u8 cnt;
    u8 valid;
    uint bad_cnt;
    uint good_cnt;
    int bad_master;
    struct ptp_port *port;
};

struct ptp_clock;
struct ptp_port;

int bmca_better(struct bmc_dataset *a, struct bmc_dataset *b);
int bmca_decision(struct ptp_clock *c, struct ptp_port *p);

struct foreign_dataset *find_best_master(struct foreign_dataset masters[]);
struct foreign_dataset *find_foreign_master(struct foreign_dataset masters[],
    struct bmc_dataset *ds);

#endif

