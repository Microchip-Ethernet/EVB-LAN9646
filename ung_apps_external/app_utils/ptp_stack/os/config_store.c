#include <stdio.h>
#include <errno.h>
#include "os.h"
#include "config_store.h"


#define MAX_PTP_PROFILES	3
#define MAX_PTP_PORTS		8

struct config_store_ptp_global {
	u8 utcOffset;
	u8 maxAllowedMissedResponses;
	u8 maxAllowedDelayFaults;
	u8 followUpReceiptTimeout;
	u8 initialSyncTimeout;
	u8 waitPdelayReqInterval;
	u8 waitSyncInterval;
};

struct config_store_ptp_global ptp_global = {
	37, 3, 5, 25, 3, 10, 5
};

#define PTP_FLAGS_2_STEP	BIT(0)
#define PTP_FLAGS_GM_CAPABLE	BIT(1)
#define PTP_FLAGS_CMLDS		BIT(2)
#define PTP_FLAGS_E2E		BIT(3)
#define PTP_FLAGS_P2P		BIT(4)

enum {
	PTP_Profile_1588,
	PTP_Profile_gPTP,
	PTP_Profile_gPTPAuto,
};

enum {
	PTP_Protocol_L2,
	PTP_Protocol_UDP,
	PTP_Protocol_UDPv6,
};

struct config_store_ptp_profile {
	u16 ports;
	u16 stackFlags;
	u8 profile;
	u8 transport;
	u8 domain;
	u8 priority1;
	u8 priority2;
	u16 offsetScaledLogVariance;
	u8 clkClass;
	u8 clkAccuracy;
	u8 majorSDOID;
	u8 minorSDOID;
	u16 vlan;
};

static struct config_store_ptp_profile ptp_profiles[MAX_PTP_PROFILES] = {
//	{ 0x1f, PTP_FLAGS_GM_CAPABLE | PTP_FLAGS_P2P,
	{ 0x1f, PTP_FLAGS_GM_CAPABLE | PTP_FLAGS_2_STEP | PTP_FLAGS_P2P,
	  PTP_Profile_1588, PTP_Protocol_UDP, 0,
	  246, 248, 0x436A, 248, 0x21, 0, 0, 0
	},
	{ 0x0, PTP_FLAGS_GM_CAPABLE | PTP_FLAGS_2_STEP | PTP_FLAGS_P2P,
	  PTP_Profile_gPTP, PTP_Protocol_L2, 0,
	  246, 248, 0x436A, 248, 0x21, 1, 0, 0
	},
	{ 0x0, PTP_FLAGS_GM_CAPABLE | PTP_FLAGS_2_STEP | PTP_FLAGS_P2P,
	  PTP_Profile_gPTPAuto, PTP_Protocol_L2, 0,
	  246, 248, 0x436A, 248, 0x21, 1, 0, 0
	},
};

struct config_store_ptp_port_profile {
	u8 announceReceiptTimeout;
	u8 syncReceiptTimeout;
	signed char logSyncInterval;
	signed char logAnnounceInterval;
	signed char logMinDelayReqInterval;
	signed char logMinPdelayReqInterval;
	signed char operLogSyncInterval;
	signed char operLogMinPdelayReqInterval;
	u16 neighborPropDelayThresh;
};

static struct config_store_ptp_port_profile ptp_port_profiles[MAX_PTP_PORTS] = {
	{ 3, 3, -3, 0, 0, 0, 2, 0, 800 },
};

struct config_store_ptp_port_setting {
	u16 receiveLatency;
	u16 transmitLatency;
	short delayAsymmetry;
	u16 peerDelay;
	u8 masterOnly;
	u8 useDelayReq;
};

static struct config_store_ptp_port_setting ptp_settings[MAX_PTP_PORTS] = {
	{ 600, 300, 0, 10, 1, 1 },
};


static int chk_index(u8 *index, u8 max)
{
	if (!*index)
		return -EINVAL;
	(*index)--;
	if (*index >= max)
		return -EINVAL;
	return 0;
}

int gptp_get_allowed(u8 *missed, u8 *faults)
{
	*missed = ptp_global.maxAllowedMissedResponses;
	*faults = ptp_global.maxAllowedDelayFaults;
	return 0;
}

int gptp_get_follow_up_timeout(u8 *timeout)
{
	*timeout = ptp_global.followUpReceiptTimeout;
	return 0;
}

int gptp_get_initial_wait(u8 *initial, u8 *wait_sync, u8 *wait_pdelay)
{
	*initial = ptp_global.initialSyncTimeout;
	*wait_sync = ptp_global.waitSyncInterval;
	*wait_pdelay = ptp_global.waitPdelayReqInterval;
	return 0;
}

int gptp_get_utc(u8 *utc)
{
	*utc = ptp_global.utcOffset;
	return 0;
}


int gptp_profile_get_profile(u8 index, u8 *profile)
{
	int rc = chk_index(&index, MAX_PTP_PROFILES);

	if (rc)
		return rc;
	*profile = ptp_profiles[index].profile;
	return 0;
}

int gptp_profile_get_sdoid(u8 index, u8 *major, u8 *minor)
{
	int rc = chk_index(&index, MAX_PTP_PROFILES);

	if (rc)
		return rc;
	*major = ptp_profiles[index].majorSDOID;
	*minor = ptp_profiles[index].minorSDOID;
	return 0;
}

int gptp_profile_get_transport(u8 index, u8 *transport)
{
	int rc = chk_index(&index, MAX_PTP_PROFILES);

	if (rc)
		return rc;
	*transport = ptp_profiles[index].transport;
	return 0;
}

int gptp_profile_get_e2e_p2p(u8 index, u8 *e2e, u8 *p2p)
{
	int rc = chk_index(&index, MAX_PTP_PROFILES);

	if (rc)
		return rc;
	*e2e = !!(ptp_profiles[index].stackFlags & PTP_FLAGS_E2E);
	*p2p = !!(ptp_profiles[index].stackFlags & PTP_FLAGS_P2P);
	return 0;
}

int gptp_profile_get_gm_capable(u8 index, u8 *capable)
{
	int rc = chk_index(&index, MAX_PTP_PROFILES);

	if (rc)
		return rc;
	*capable = !!(ptp_profiles[index].stackFlags & PTP_FLAGS_GM_CAPABLE);
	return 0;
}

int gptp_profile_get_two_step(u8 index, u8 *two_step)
{
	int rc = chk_index(&index, MAX_PTP_PROFILES);

	if (rc)
		return rc;
	*two_step = !!(ptp_profiles[index].stackFlags & PTP_FLAGS_2_STEP);
	return 0;
}


int gptp_profile_get_clock_prio(u8 index, u8 *prio1, u8 *prio2)
{
	int rc = chk_index(&index, MAX_PTP_PROFILES);

	if (rc)
		return rc;
	*prio1 = ptp_profiles[index].priority1;
	*prio2 = ptp_profiles[index].priority2;
	return 0;
}

int gptp_profile_get_clock_prop(u8 index, u8 *clk_class, u8 *accuracy,
	u16 *variance)
{
	int rc = chk_index(&index, MAX_PTP_PROFILES);

	if (rc)
		return rc;
	*clk_class = ptp_profiles[index].clkClass;
	*accuracy = ptp_profiles[index].clkAccuracy;
	*variance = ptp_profiles[index].offsetScaledLogVariance;
	return 0;
}

int gptp_profile_get_domain(u8 index, u8 *domain, u16 *ports, u16 *vlan)
{
	int rc = chk_index(&index, MAX_PTP_PROFILES);

	if (rc)
		return rc;
	*domain = ptp_profiles[index].domain;
	*ports = ptp_profiles[index].ports;
	*vlan = ptp_profiles[index].vlan;
	return 0;
}


int gptp_port_profile_get_oper(u8 index, signed char *sync,
	signed char *pdelay)
{
	int rc = chk_index(&index, MAX_PTP_PORTS);

	if (rc)
		return rc;
	*sync = ptp_port_profiles[index].operLogSyncInterval;
	*pdelay = ptp_port_profiles[index].operLogMinPdelayReqInterval;
	return 0;
}


int gptp_port_profile_get_delay(u8 index, signed char *delay)
{
	int rc = chk_index(&index, MAX_PTP_PORTS);

	if (rc)
		return rc;
	*delay = ptp_port_profiles[index].logMinDelayReqInterval;
	return 0;
}

int gptp_port_profile_get_pdelay(u8 index, signed char *pdelay)
{
	int rc = chk_index(&index, MAX_PTP_PORTS);

	if (rc)
		return rc;
	*pdelay = ptp_port_profiles[index].logMinPdelayReqInterval;
	return 0;
}

int gptp_port_profile_get_announce(u8 index, signed char *announce, u8 *timeout)
{
	int rc = chk_index(&index, MAX_PTP_PORTS);

	if (rc)
		return rc;
	*announce = ptp_port_profiles[index].logAnnounceInterval;
	*timeout = ptp_port_profiles[index].announceReceiptTimeout;
	return 0;
}

int gptp_port_profile_get_sync(u8 index, signed char *sync, u8 *timeout)
{
	int rc = chk_index(&index, MAX_PTP_PORTS);

	if (rc)
		return rc;
	*sync = ptp_port_profiles[index].logSyncInterval;
	*timeout = ptp_port_profiles[index].syncReceiptTimeout;
	return 0;
}

int gptp_port_profile_get_delay_thresh(u8 index, u16 *pdelay)
{
	int rc = chk_index(&index, MAX_PTP_PORTS);

	if (rc)
		return rc;
	*pdelay = ptp_port_profiles[index].neighborPropDelayThresh;
	return 0;
}


int gptp_port_get_latency(u8 index, u16 *rx, u16 *tx, short *asym)
{
	int rc = chk_index(&index, MAX_PTP_PORTS);

	if (rc)
		return rc;
	*rx = ptp_settings[index].receiveLatency;
	*tx = ptp_settings[index].transmitLatency;
	*asym = ptp_settings[index].delayAsymmetry;
	return 0;
}


int gptp_port_get_master(u8 index, u8 *master)
{
	int rc = chk_index(&index, MAX_PTP_PORTS);

	if (rc)
		return rc;
	*master = ptp_settings[index].masterOnly;
	return 0;
}

int gptp_port_get_peer_delay(u8 index, u16 *delay)
{
	int rc = chk_index(&index, MAX_PTP_PORTS);

	if (rc)
		return rc;
	*delay = ptp_settings[index].peerDelay;
	return 0;
}

int gptp_port_get_use_delay(u8 index, u8 *use)
{
	int rc = chk_index(&index, MAX_PTP_PORTS);

	if (rc)
		return rc;
	*use = ptp_settings[index].useDelayReq;
	return 0;
}


void chk_global_param(char *param, int val)
{
	if (!strcmp(param, "utcOffset")) {
		ptp_global.utcOffset = (u8)val;
	} else if (!strcmp(param, "maxAllowedMissedResponses")) {
		ptp_global.maxAllowedMissedResponses = (u8)val;
	} else if (!strcmp(param, "maxAllowedDelayFaults")) {
		ptp_global.maxAllowedDelayFaults = (u8)val;
	} else if (!strcmp(param, "followUpReceiptTimeout")) {
		ptp_global.followUpReceiptTimeout = (u8)val;
	} else if (!strcmp(param, "initialSyncTimeout")) {
		ptp_global.initialSyncTimeout = (u8)val;
	} else if (!strcmp(param, "waitPdelayReqInterval")) {
		ptp_global.waitPdelayReqInterval = (u8)val;
	} else if (!strcmp(param, "waitSyncInterval")) {
		ptp_global.waitSyncInterval = (u8)val;
	}
}

void chk_profile_param(u8 index, char *param, int val)
{
	int rc = chk_index(&index, MAX_PTP_PROFILES);

	if (rc)
		return;
	if (!strcmp(param, "ports")) {
		ptp_profiles[index].ports = (u16)val;
	} else if (!strcmp(param, "profile")) {
		ptp_profiles[index].profile = (u8)val;
	} else if (!strcmp(param, "delayMechanism")) {
		ptp_profiles[index].stackFlags &=
			~(PTP_FLAGS_E2E | PTP_FLAGS_P2P);
		if (val == 1)
			ptp_profiles[index].stackFlags |= PTP_FLAGS_E2E;
		else if (val == 2)
			ptp_profiles[index].stackFlags |= PTP_FLAGS_P2P;
	} else if (!strcmp(param, "two_step")) {
		ptp_profiles[index].stackFlags &= ~PTP_FLAGS_2_STEP;
		if (val)
			ptp_profiles[index].stackFlags |= PTP_FLAGS_2_STEP;
	} else if (!strcmp(param, "gm_capable")) {
		ptp_profiles[index].stackFlags &= ~PTP_FLAGS_GM_CAPABLE;
		if (val)
			ptp_profiles[index].stackFlags |= PTP_FLAGS_GM_CAPABLE;
	} else if (!strcmp(param, "cmlds")) {
		ptp_profiles[index].stackFlags &= ~PTP_FLAGS_CMLDS;
		if (val)
			ptp_profiles[index].stackFlags |= PTP_FLAGS_CMLDS;
	} else if (!strcmp(param, "transport")) {
		ptp_profiles[index].transport = (u8)val;
	} else if (!strcmp(param, "domain")) {
		ptp_profiles[index].domain = (u8)val;
	} else if (!strcmp(param, "priority1")) {
		ptp_profiles[index].priority1 = (u8)val;
	} else if (!strcmp(param, "priority2")) {
		ptp_profiles[index].priority2 = (u8)val;
	} else if (!strcmp(param, "offsetScaledLogVariance")) {
		ptp_profiles[index].offsetScaledLogVariance = (u16)val;
	} else if (!strcmp(param, "clkClass")) {
		ptp_profiles[index].clkClass = (u8)val;
	} else if (!strcmp(param, "clkAccuracy")) {
		ptp_profiles[index].clkAccuracy = (u8)val;
	} else if (!strcmp(param, "majorSDOID")) {
		ptp_profiles[index].majorSDOID = (u8)val;
	} else if (!strcmp(param, "minorSDOID")) {
		ptp_profiles[index].minorSDOID = (u8)val;
	} else if (!strcmp(param, "vlan")) {
		ptp_profiles[index].vlan = (u16)val;
	}
}

void chk_port_param(u8 index, char *param, int val)
{
	int rc = chk_index(&index, MAX_PTP_PORTS);
	u8 first, last;

	if (rc && !index) {
		first = 0;
		last = MAX_PTP_PORTS;
	} else if (!rc) {
		first = index;
		last = first + 1;
	} else {
		return;
	}
	if (!strcmp(param, "announceReceiptTimeout")) {
		for (index = first; index < last; index++)
			ptp_port_profiles[index].announceReceiptTimeout =
				(u8)val;
	} else if (!strcmp(param, "syncReceiptTimeout")) {
		for (index = first; index < last; index++)
			ptp_port_profiles[index].syncReceiptTimeout = (u8)val;
	} else if (!strcmp(param, "logSyncInterval")) {
		for (index = first; index < last; index++)
			ptp_port_profiles[index].logSyncInterval =
				(signed char)val;
	} else if (!strcmp(param, "logAnnounceInterval")) {
		for (index = first; index < last; index++)
			ptp_port_profiles[index].logAnnounceInterval =
				(signed char)val;
	} else if (!strcmp(param, "logMinDelayReqInterval")) {
		for (index = first; index < last; index++)
			ptp_port_profiles[index].logMinDelayReqInterval =
				(signed char)val;
	} else if (!strcmp(param, "logMinPdelayReqInterval")) {
		for (index = first; index < last; index++)
			ptp_port_profiles[index].logMinPdelayReqInterval =
				(signed char)val;
	} else if (!strcmp(param, "operLogSyncInterval")) {
		for (index = first; index < last; index++)
			ptp_port_profiles[index].operLogSyncInterval =
				(signed char)val;
	} else if (!strcmp(param, "operLogMinPdelayReqInterval")) {
		for (index = first; index < last; index++)
			ptp_port_profiles[index].operLogMinPdelayReqInterval =
				(signed char)val;
	} else if (!strcmp(param, "neighborPropDelayThresh")) {
		for (index = first; index < last; index++)
			ptp_port_profiles[index].neighborPropDelayThresh =
				(u16)val;
	} else if (!strcmp(param, "receiveLatency")) {
		for (index = first; index < last; index++)
			ptp_settings[index].receiveLatency = (u16)val;
	} else if (!strcmp(param, "transmitLatency")) {
		for (index = first; index < last; index++)
			ptp_settings[index].transmitLatency = (u16)val;
	} else if (!strcmp(param, "delayAsymmetry")) {
		for (index = first; index < last; index++)
			ptp_settings[index].delayAsymmetry = (short)val;
	} else if (!strcmp(param, "peerDelay")) {
		for (index = first; index < last; index++)
			ptp_settings[index].peerDelay = (u16)val;
	} else if (!strcmp(param, "masterOnly")) {
		for (index = first; index < last; index++)
			ptp_settings[index].masterOnly = (u8)val;
	} else if (!strcmp(param, "useDelayReq")) {
		for (index = first; index < last; index++)
			ptp_settings[index].useDelayReq = (u8)val;
	}
}

int init_config(char *name)
{
	FILE *ifp;
	int n;
	char line[80];
	int val;
	char param[80];
	char strval[80];
	char *first;
	int port = -1;
	int profile = -1;

	for (n = 1; n < MAX_PTP_PORTS; n++)
		memcpy(&ptp_port_profiles[n], &ptp_port_profiles[0],
		       sizeof(struct config_store_ptp_port_profile));
	for (n = 1; n < MAX_PTP_PORTS; n++)
		memcpy(&ptp_settings[n], &ptp_settings[0],
		       sizeof(struct config_store_ptp_port_setting));
	ifp = fopen(name, "rt");
	if (!ifp) {
		return -1;
	}
	while (fgets(line, sizeof(line), ifp)) {
		first = strchr(line, '[');
		if (first) {
			char *last;

			++first;
			strncpy(param, first, sizeof(param) - 1);
			param[sizeof(param) - 1] = '\0';
			last = strchr(param, ']');
			if (last)
				*last = '\0';
			first = strstr(param, "port");
			if (first) {
				n = sscanf(param, "%s %u", strval, &val);
				if (n == 2) {
					port = val;
				}
			} else if (!strcmp(param, "global")) {
				port = -1;
				profile = -1;
			} else {
				first = strstr(param, "profile");
				if (first) {
					n = sscanf(param, "%s %u", strval, &val);
					if (n == 2) {
						profile = val;
						port = -1;
					}
				}
			}
			continue;
		}
		n = sscanf(line, "%s 0x%x", param, &val);
		if (n == 2) {
			if (port >= 0)
				chk_port_param(port, param, val);
			else if (profile >= 0)
				chk_profile_param(profile, param, val);
			else
				chk_global_param(param, val);
			continue;
		}
		n = sscanf(line, "%s %d", param, &val);
		if (n == 2) {
			if (port >= 0)
				chk_port_param(port, param, val);
			else if (profile >= 0)
				chk_profile_param(profile, param, val);
			else
				chk_global_param(param, val);
			continue;
		}
		n = sscanf(line, "%s %s", param, strval);
		if (n == 2) {
			val = 0;
			if (!strcmp(strval, "E2E")) {
				val = 1;
			} else if (!strcmp(strval, "P2P")) {
				val = 2;
			} else if (!strcmp(strval, "L2")) {
				val = 0;
			} else if (!strcmp(strval, "UDP")) {
				val = 1;
			} else if (!strcmp(strval, "UDPv6")) {
				val = 2;
			}
			if (port >= 0)
				chk_port_param(port, param, val);
			else if (profile >= 0)
				chk_profile_param(profile, param, val);
			else
				chk_global_param(param, val);
			continue;
		}
	}
	fclose(ifp);
	return 0;
}

