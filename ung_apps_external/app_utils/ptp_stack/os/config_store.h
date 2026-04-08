

#ifndef _CONFIG_STORE_H
#define _CONFIG_STORE_H

int gptp_profile_get_profile(u8 index, u8 *profile);
int gptp_profile_get_sdoid(u8 index, u8 *major, u8 *minor);
int gptp_profile_get_transport(u8 index, u8 *transport);
int gptp_profile_get_e2e_p2p(u8 index, u8 *e2e, u8 *p2p);
int gptp_profile_get_gm_capable(u8 index, u8 *capable);
int gptp_profile_get_two_step(u8 index, u8 *two_step);

int gptp_get_allowed(u8 *missec, u8 *faults);
int gptp_get_follow_up_timeout(u8 *timeout);
int gptp_get_initial_wait(u8 *initial, u8 *wait_sync, u8 *wait_pdelay);
int gptp_get_utc(u8 *utc);

int gptp_profile_get_clock_prio(u8 index, u8 *prio1, u8 *prio2);
int gptp_profile_get_clock_prop(u8 index, u8 *clk_class, u8 *accuracy,
	u16 *variance);
int gptp_profile_get_domain(u8 index, u8 *domain, u16 *ports, u16 *vlan);

int gptp_port_profile_get_oper(u8 index, signed char *sync,
	signed char *pdelay);

int gptp_port_profile_get_delay(u8 index, signed char *delay);
int gptp_port_profile_get_pdelay(u8 index, signed char *pdelay);
int gptp_port_profile_get_announce(u8 index, signed char *announce,
	u8 *timeout);
int gptp_port_profile_get_sync(u8 index, signed char *sync, u8 *timeout);
int gptp_port_profile_get_delay_thresh(u8 index, u16 *pdelay);

int gptp_port_get_latency(u8 index, u16 *rx, u16 *tx, short *asym);

int gptp_port_get_master(u8 index, u8 *master);
int gptp_port_get_peer_delay(u8 index, u16 *delay);
int gptp_port_get_use_delay(u8 index, u8 *use);

int init_config(char *file);

#endif

