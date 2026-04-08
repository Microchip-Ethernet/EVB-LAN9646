

#ifndef PTP_PTU_H
#define PTP_PTU_H

#define NANOSEC_IN_SEC                  1000000000

#define SCALED_NANOSEC_IN_SEC           ((s64)(1000000000ULL << SCALED_NANOSEC_S))

struct ptu {
    s64 sec;
    s64 nsec;
};

int ptu_str(struct ptu *u, char *str, size_t size);
int s64_str(s64 n, char *str, size_t size);
int float_str(double d, char *str, size_t size);
int ptu_after(struct ptu *u1, struct ptu *u2);
int ptu_empty(struct ptu *u);
void ptu_zero(struct ptu *u);
s64 ptu_get_nsec(struct ptu *u);
void ptu_add(struct ptu *u1, struct ptu *u2, struct ptu *u3);
void ptu_sub(struct ptu *u1, struct ptu *u2, struct ptu *u3);
void ptu_div_int(struct ptu *u1, s64 divisor, struct ptu *u3);
void ptu_div_sec(struct ptu *u1, struct ptu *u2, struct ptu *u3);
double ptu_div_ratio(struct ptu *u1, struct ptu *u2);
void ptu_mult(struct ptu *u1, double r, struct ptu *u3);

int float_to_ratio(double ratio);
double ratio_to_float(int ratio);

#endif
