
#ifndef PTP_CONFIG_H
#define PTP_CONFIG_H

int ptp_config_get_gptp_mode(void);
u8 ptp_config_get_slave_port(void);
bool ptp_config_using_p2p(void);
bool ptp_config_using_e2e(void);
bool ptp_config_using_two_step(void);


void ptp_init_config_ds(u8 index, struct ptp_default_ds *dds);
void ptp_init_port_ds(struct ptp_port *p);
void ptp_update_parent_ds(struct ptp_clock *c, struct foreign_dataset *f);

#endif

