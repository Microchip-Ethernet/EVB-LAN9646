#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#if 0
#define DBG_CGI
#endif

#define FORM_NAME_SIZE		40
#define NUM_OF_FORM_VAL		80

#ifdef DBG_CGI
static FILE *f;
#endif
static FILE *sw;
int eth;

struct name_val {
	char name[FORM_NAME_SIZE];
	char val[FORM_NAME_SIZE];
} forms[NUM_OF_FORM_VAL];

static int form_cnt;

static int cmp_str(char *name, char *str)
{
	return strncmp(name, str, strlen(str));
}

static void proc_read(char *name, char *buf, int len)
{
	char proc[80];
	int n;

	n = snprintf(proc, sizeof(proc), "/sys/class/net/eth%d/web/%s", eth,
		     name);
	sw = fopen(proc, "r");
	if (sw) {
		n = fread(buf, 1, len, sw);
		buf[n] = '\0';
		printf("%s", buf);
		fclose(sw);
	}
}

static void proc_write(char *name, char *buf, int len)
{
	char proc[80];
	int n;

	n = snprintf(proc, sizeof(proc), "/sys/class/net/eth%d/web/%s", eth,
		     name);
	sw = fopen(proc, "w");
	if (sw) {
		n = fwrite(buf, 1, len, sw);
		fclose(sw);
	}
}

static void proc_sw_read(char *name, char *buf, int len)
{
	char proc[80];
	int n;

	n = snprintf(proc, sizeof(proc), "/sys/class/net/eth%d/%s", eth,
		     name);
	sw = fopen(proc, "r");
	if (sw) {
		n = fread(buf, 1, len, sw);
		buf[n] = '\0';
		printf("%s", buf);
		fclose(sw);
	}
}

static void proc_sw_write(char *name, char *buf, int len)
{
	char proc[80];
	int n;

	n = snprintf(proc, sizeof(proc), "/sys/class/net/eth%d/%s", eth,
		     name);
	sw = fopen(proc, "w");
	if (sw) {
		n = fwrite(buf, 1, len, sw);
		fclose(sw);
	}
}

static void get_device(char *dev)
{
	char device[80];
	char buf[40];
	int i, j, n;
	FILE *ifp;

	for (i = 0; i < 4; i++) {
		for (j = 0; j < 4; j++) {
			sprintf(dev, "spi%u.%u", i, j);
			n = snprintf(device, sizeof(device),
				     "/sys/bus/spi/devices/%s/modalias", dev);
			ifp = fopen(device, "rb");
			if (ifp) {
				n = fread(buf, 1, sizeof(buf), ifp);
				buf[n] = '\0';
				fclose(ifp);
				if (strstr(buf, "spi:ksz") ||
				    strstr(buf, "spi:lan"))
					return;
			}
		}
	}
	dev[0] = '\0';
}

static int get_reg(int fd, unsigned int reg, size_t cnt, unsigned int *buf)
{
	off_t n;

	n = lseek(fd, reg, SEEK_SET);
	if (n == (off_t) -1)
		return n;
	n = read(fd, buf, cnt);
	return n;
}

static int determine_reg_size(int fd)
{
	unsigned int data[1];
	int n;

	/* See if register width is 32-bit and returns only 2 bytes. */
	n = get_reg(fd, 2, 4, data);
	if (n < 0) {
		return -1;
	}

	/* Register width can be 16-bit or 8-bit. */
	if (n == 4) {

		/* See if register width is 8-bit and returns only 1 byte. */
		n = get_reg(fd, 1, 4, data);
		if (n == 4)
			n = 1;
		else
			n = 2;
	} else {
		n = 4;
	}
	return n;
}

static void reg_read(unsigned int reg)
{
	char device[80];
	char name[8];
	unsigned int val;
	int cnt, fd, n;

	get_device(name);
	n = snprintf(device, sizeof(device),
		     "/sys/bus/spi/devices/%s/registers", name);
	fd = open(device, O_RDWR);
	if (fd < 0)
		return;
	cnt = determine_reg_size(fd);
	if (cnt < 0)
		return;
	n = get_reg(fd, reg, cnt, &val);
	switch (cnt) {
	case 4:
		printf("%08x", val);
		break;
	case 2:
		printf("%04x", val);
		break;
	default:
		printf("%02x", val);
		break;
	}
	close(fd);
}

static void reg_write(unsigned int reg, unsigned int val)
{
	char device[80];
	char name[8];
	int cnt, fd, n;

	get_device(name);
	n = snprintf(device, sizeof(device),
		     "/sys/bus/spi/devices/%s/registers", name);
	fd = open(device, O_RDWR);
	if (fd < 0)
		return;
	cnt = determine_reg_size(fd);
	if (cnt < 0)
		return;
	n = lseek(fd, reg, SEEK_SET);
	if (n == (off_t) -1)
		return;
	n = write(fd, &val, cnt);
	close(fd);
}

static void proc_get_cmd(char *buf, size_t len)
{
	char *cmd = strstr(buf, "CMD=");
	char val[4096];
	char *ptr;
	int i, n;

	if (!cmd)
		return;
	cmd = strchr(buf, '=');
	cmd++;
	len -= 4;
	if (!strncmp(cmd, "DevInfo", len)) {
		proc_read("dev_info", val, sizeof(val));
	} else if (!strncmp(cmd, "TgtInfo", len)) {
		proc_read("tgt_info", val, sizeof(val));
	} else if (!strncmp(cmd, "GVLANCfg", len)) {
		int set;

		set = -1;
		for (i = 0; i < form_cnt; i++) {
			if (!cmp_str(forms[i].name, "VlanState")) {
				if (cmp_str(forms[i].val, "x"))
					set = strtol(forms[i].val, &ptr, 10);
			}
		}
		if (set >= 0) {
			n = snprintf(val, sizeof(val), "%u", set);
			proc_write("vlan", val, n);
		}
		proc_read("vlan", val, sizeof(val));
	} else if (!strncmp(cmd, "GJumboSupport", len)) {
		int set;

		set = -1;
		for (i = 0; i < form_cnt; i++) {
			if (!cmp_str(forms[i].name, "JumboFrame")) {
				if (cmp_str(forms[i].val, "x"))
					set = strtol(forms[i].val, &ptr, 10);
			}
		}
		if (set >= 0) {
			n = snprintf(val, sizeof(val), "%u", set);
			proc_write("jumbo_packet", val, n);
		}
		proc_read("jumbo_packet", val, sizeof(val));
	} else if (!strncmp(cmd, "GMTUInfo", len)) {
		int mtu;

		mtu = -1;
		for (i = 0; i < form_cnt; i++) {
			if (!cmp_str(forms[i].name, "MTUSize")) {
				mtu = strtol(forms[i].val, &ptr, 10);
			}
		}
		if (mtu >= 0) {
			n = snprintf(val, sizeof(val), "%u", mtu);
			proc_write("mtu", val, n);
		}
		proc_read("mtu", val, sizeof(val));
	} else if (!strncmp(cmd, "VlanInfo", len)) {
		proc_read("vlan_table", val, sizeof(val));
	} else if (!strncmp(cmd, "VlanCfg", len)) {
		int vid, fid, ports, untag, valid;

		valid = -1;
		for (i = 0; i < form_cnt; i++) {
			if (!cmp_str(forms[i].name, "Vid")) {
				vid = strtol(forms[i].val, &ptr, 10);
			} else if (!cmp_str(forms[i].name, "Fid")) {
				fid = strtol(forms[i].val, &ptr, 10);
			} else if (!cmp_str(forms[i].name, "VlanMembers")) {
				ports = strtol(forms[i].val, &ptr, 10);
			} else if (!cmp_str(forms[i].name, "UntagMembers")) {
				untag = strtol(forms[i].val, &ptr, 10);
			} else if (!cmp_str(forms[i].name, "VlanAction")) {
				if (!cmp_str(forms[i].val, "Add"))
					valid = 1;
				else if (!cmp_str(forms[i].val, "Del"))
					valid = 0;
			}
		}
		if (valid >= 0) {
			n = snprintf(val, sizeof(val), "%u %u %u %u %u",
				     vid, fid, ports, untag, valid);
			proc_write("vlan_table", val, n);
		}
	} else if (!strncmp(cmd, "PVIDCfg", len)) {
		int ingress, port, prio, vid;
		char accept[4];

		port = -1;
		for (i = 0; i < form_cnt; i++) {
			if (!cmp_str(forms[i].name, "PVIDPort")) {
				port = strtol(forms[i].val, &ptr, 10);
				break;
			}
		}
		vid = -1;
		for (i = 0; i < form_cnt; i++) {
			if (!cmp_str(forms[i].name, "Pvid") &&
			    forms[i].name[4] == port + '0') {
				if (!cmp_str(forms[i].val, "undefined"))
					break;
				vid = strtol(forms[i].val, &ptr, 10);
			} else if (!cmp_str(forms[i].name, "IngressFilter")) {
				ingress = strtol(forms[i].val, &ptr, 10);
			} else if (!cmp_str(forms[i].name, "FrameAccType")) {
				strncpy(accept, forms[i].val,
					sizeof(accept) - 1);
			} else if (!cmp_str(forms[i].name, "PortPriority")) {
				prio = strtol(forms[i].val, &ptr, 10);
			}
		}
		if (port > 0 && vid >= 0) {
			n = snprintf(val, sizeof(val), "%u %u %u %u %s",
				     port, vid, prio, ingress, accept);
			proc_write("pvid", val, n);
		}
		proc_read("pvid", val, sizeof(val));
	} else if (!strncmp(cmd, "PortCfg", len)) {
		int duplex, port, speed;

		port = -1;
		for (i = 0; i < form_cnt; i++) {
			if (!cmp_str(forms[i].name, "Port")) {
				port = strtol(forms[i].val, &ptr, 10);
				break;
			}
		}
		speed = -1;
		for (i = 0; i < form_cnt; i++) {
			if (!cmp_str(forms[i].name, "LinkSpeed")) {
				if (!cmp_str(forms[i].val, "Auto")) {
					speed = 0;
					duplex = 0;
				} else if (!cmp_str(forms[i].val, "1000MFD")) {
					speed = 1000;
					duplex = 2;
				} else if (!cmp_str(forms[i].val, "1000MHD")) {
					speed = 1000;
					duplex = 1;
				} else if (!cmp_str(forms[i].val, "100MFD")) {
					speed = 100;
					duplex = 2;
				} else if (!cmp_str(forms[i].val, "100MHD")) {
					speed = 100;
					duplex = 1;
				} else if (!cmp_str(forms[i].val, "10MFD")) {
					speed = 10;
					duplex = 2;
				} else if (!cmp_str(forms[i].val, "10MHD")) {
					speed = 10;
					duplex = 1;
				}
			}
		}
		if (port > 0 && speed >= 0) {
			n = snprintf(val, sizeof(val), "%u %u %u",
				     port, speed, duplex);
			proc_write("port_status", val, n);
		}
		proc_read("port_status", val, sizeof(val));
	} else if (!strncmp(cmd, "SWStats", len)) {
		char action[8];
		int clear = -1;

		for (i = 0; i < form_cnt; i++) {
			if (!cmp_str(forms[i].name, "FormCmd")) {
				strncpy(action, forms[i].val,
					sizeof(action) - 1);
				if (!cmp_str(action, "Clear"))
					clear = 1;
			}
		}
		if (clear == 1)
			proc_sw_write("sw/mib", "0", 1);
		proc_sw_read("sw/mib", val, sizeof(val));
	} else if (!strncmp(cmd, "PortStats", len)) {
		char mib[20];
		char action[8];
		int port = -1;
		int clear = -1;

		for (i = 0; i < form_cnt; i++) {
			if (!cmp_str(forms[i].name, "FormCmd")) {
				strncpy(action, forms[i].val,
					sizeof(action) - 1);
				if (!cmp_str(action, "Clear"))
					clear = 1;
			} else if (!cmp_str(forms[i].name, "StatPort")) {
				port = strtol(forms[i].val, &ptr, 10);
				snprintf(mib, sizeof(mib),
					 "sw%u/%u_mib", port, port);
			}
		}
		if (port >= 0) {
			if (clear == 1)
				proc_sw_write(mib, "0", 1);
			proc_sw_read(mib, val, sizeof(val));
		}
	} else if (!strncmp(cmd, "DynamicMACInfo", len)) {
		char table[20];
		char action[8];
		int clear = -1;

		for (i = 0; i < form_cnt; i++) {
			if (!cmp_str(forms[i].name, "FormCmd")) {
				strncpy(action, forms[i].val,
					sizeof(action) - 1);
				if (!cmp_str(action, "Clear"))
					clear = 1;
			} else if (!cmp_str(forms[i].name, "TblType")) {
				strncpy(table, forms[i].val,
					sizeof(table) - 1);
				if (clear < 0)
					clear = 0;
			}
		}
		if (clear >= 0) {
			if (clear == 1)
				proc_write(table, "0", 1);
			proc_read(table, val, sizeof(val));
		}
	} else if (!strncmp(cmd, "StaticMacInfo", len)) {
		proc_read("static_cfg", val, sizeof(val));
	} else if (!strncmp(cmd, "StaticMacCfg", len)) {
		int dst, fid, mstp, override, prio, src, type, use_fid;
		int index, ports, valid;
		char addr[20];

		dst = fid = mstp = override = prio = src = type = use_fid = 0;
		valid = -1;
		for (i = 0; i < form_cnt; i++) {
			if (!cmp_str(forms[i].name, "ALUIndex"))
				index = strtol(forms[i].val, &ptr, 10);
			else if (!cmp_str(forms[i].name, "ALUMACAddr"))
				strncpy(addr, forms[i].val,
					sizeof(addr) - 1);
			else if (!cmp_str(forms[i].name, "ALUMembers"))
				ports = strtol(forms[i].val, &ptr, 10);
			else if (!cmp_str(forms[i].name, "ALUAction")) {
				if (!cmp_str(forms[i].val, "Add"))
					valid = 1;
				else
					valid = 0;
			}
		}
		if (valid >= 0) {
			n = snprintf(val, sizeof(val),
				     "%u %u %s 0x%x %u %u %u %u %u %u %u %u",
				     index, type, addr, ports,
				     dst, src, mstp, prio, use_fid, fid,
				     override, valid);
			if (n > sizeof(val))
				n = sizeof(val) - 1;
			proc_write("static_cfg", val, n);
		}
		n = snprintf(val, sizeof(val),
			     "%x:%s#%x", index, addr, ports);
		printf("%s", val);
	} else if (!strncmp(cmd, "SWDebug", len)) {
		char action[8];
		int reg, val;

		reg = -1;
		for (i = 0; i < form_cnt; i++) {
			if (!cmp_str(forms[i].name, "access"))
				strncpy(action, forms[i].val,
					sizeof(action) - 1);
			else if (!cmp_str(forms[i].name, "swReg"))
				reg = strtol(forms[i].val, &ptr, 16);
			else if (!cmp_str(forms[i].name, "RegVal"))
				val = strtol(forms[i].val, &ptr, 16);
		}
		if (reg >= 0) {
			if (!cmp_str(action, "get"))
				reg_read(reg);
			else if (!cmp_str(action, "set"))
				reg_write(reg, val);
		}
	}
}

static int get_name_val(char *buf, size_t len, struct name_val *form)
{
	char *delim, *tmp;
	int i = 0;

	tmp = buf;
	do {
		tmp = strstr(tmp, "Content-Disposition: ");
		if (!tmp) {
#ifdef DBG_CGI
			if (f) {
				fprintf(f, "L:%d\n", i);
				fclose(f);
				f = fopen("/tmp/web.log", "at");
			}
#endif
			return i;
		}
		tmp = strstr(tmp, "form-data; ");
		if (!tmp)
			return i;
		tmp = strstr(tmp, "name=");
		if (!tmp)
			return i;
		tmp = strchr(tmp, '"');
		if (!tmp)
			return i;
		tmp++;
		delim = strchr(tmp, '"');
		if (!delim)
			return i;
		*delim = '\0';
		strncpy(form[i].name, tmp, FORM_NAME_SIZE - 1);
		tmp = delim + 1;
#ifdef DBG_CGI
		if (f)
			fprintf(f, "name=[%s]\n", form[i].name);
#endif
		tmp = strchr(tmp, '\n');
		if (!tmp)
			return i;
		tmp++;
		tmp = strchr(tmp, '\n');
		if (!tmp)
			return i;
		tmp++;
		if (*tmp != '\r' && *tmp != '\n') {
			delim = strchr(tmp, '\r');
			if (!delim)
				delim = strchr(tmp, '\n');
			if (!delim)
				return i;
			*delim = '\0';
			delim++;
			strncpy(form[i].val, tmp, FORM_NAME_SIZE - 1);
#ifdef DBG_CGI
			if (f) {
				fprintf(f, "val=[%s]\n", forms[i].val);
				fclose(f);
				f = fopen("/tmp/web.log", "at");
			}
#endif
			tmp = strchr(delim, '\n');
			tmp++;
		}
		while (*tmp == '\r' || *tmp == '\n')
			tmp++;
		i++;
	} while (*tmp != '\0' && i < NUM_OF_FORM_VAL);
	return i;
}

int main(int argc, char *argv[])
{
	char buf[4096];
	char loc[80];
	char *env;
	size_t n;

	/* Check whether switch driver supports web GUI. */
	eth = 0;
	sw = fopen("/sys/class/net/eth0/web/dev_info", "r");
	if (!sw) {
		eth = 1;
		sw = fopen("/sys/class/net/eth1/web/dev_info", "r");
	}
	if (!sw)
		return 1;
	fclose(sw);

#ifdef DBG_CGI
	f = fopen("/tmp/web.log", "at");
#endif
	printf("Content-type: text/html\n\n");
	env = getenv("CONTENT_LENGTH");
	if (env) {
		char *ptr;
		int len;

		len = strtol(env, &ptr, 10);
		if (len > 0) {
			if (len > sizeof(buf) - 1)
				len = sizeof(buf) - 1;
			n = fread(buf, 1, len, stdin);
			if (n > 0) {
				buf[n] = '\0';
#ifdef DBG_CGI
#if 0
				if (f)
					fprintf(f, "n:%d\n%s\n", n, buf);
#endif
#endif
				form_cnt = get_name_val(buf, len, forms);
			}
		}
	}
	env = getenv("QUERY_STRING");
	if (env) {
#ifdef DBG_CGI
		if (f)
			fprintf(f, "query: %s\n", env);
#endif
		strncpy(buf, env, sizeof(buf) - 1);
		proc_get_cmd(buf, strlen(buf));
	}

#ifdef DBG_CGI
	if (f)
		fclose(f);
#endif
	return 0;
}
