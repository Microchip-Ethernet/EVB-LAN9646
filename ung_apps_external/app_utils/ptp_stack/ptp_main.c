#include <mqueue.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <stdio.h>

static int dbg_on = 1;
static int log_on = 1;

#define PRINT_MSG_SIZE			(80 * 10)
#define LOG_MSG_SIZE			(80 * 5)

static char dbg_msg_buf[PRINT_MSG_SIZE];
static char *dbg_msg_ptr = dbg_msg_buf;
static int dbg_msg_cnt;

void dbg_msg(char *fmt, ...)
{
	va_list args;
	int left, n;
	char **msg;

	msg = &dbg_msg_ptr;
	left = PRINT_MSG_SIZE - dbg_msg_cnt;
	if (left <= 4)
		return;

	va_start(args, fmt);
	n = vsnprintf(*msg, left, fmt, args);
	va_end(args);
	if (n > 0) {
		if (left > n)
			left = n;
		dbg_msg_cnt += left;
		*msg += left;
	}
}

static void print_dbg_msg(void)
{
	if (dbg_on && dbg_msg_cnt)
		printf("%s", dbg_msg_buf);
	dbg_msg_ptr = dbg_msg_buf;
	dbg_msg_cnt = 0;
}

void exception_log(uint sec, uint nsec, char *fmt, ...);

#include "ptp_stack.c"

static struct ksz_timer_info log_timer;
static char log_msg_buf[LOG_MSG_SIZE];
static char *log_msg_ptr = log_msg_buf;
static int log_msg_cnt;

void print_log(void)
{
	if (log_on && log_msg_cnt)
		printf("%s", log_msg_buf);
	log_msg_ptr = log_msg_buf;
	log_msg_cnt = 0;
}

void exception_log(uint sec, uint nsec, char *fmt, ...)
{
	char log[240], time_log[40];
	va_list args;
	int left, n;
	char **msg;

	if (!log_on)
		return;
	msg = &log_msg_ptr;
	left = LOG_MSG_SIZE - log_msg_cnt;
	if (left <= 40) {
		print_log();
		left = LOG_MSG_SIZE;
	}

	if (!sec && !nsec) {
		n = get_clock(dev[1].fd, &sec, &nsec, 0);
		if (n)
			nsec = sec = 100;
	}
	n = snprintf(time_log, sizeof(time_log), "%u:%9u> ", sec, nsec);
	va_start(args, fmt);
	n = vsnprintf(log, left - n - 1, fmt, args);
	va_end(args);
	if (n > 0) {
		n = snprintf(log_msg_ptr, left, "%s%s\n", time_log, log);
		log_msg_cnt += n;
		log_msg_ptr += n;
	}
}

static void log_func(TimerHandle_t xTimer)
{
	print_dbg_msg();
	print_log();
}

void exit_stack(void)
{
	struct ptp_clock *c;
	u8 i;

	for (i = 0; i < 3; i++) {
		c = ptp_clock_ptr[i];
		if (c && c->ports) {
			clock_stop(c);
			delete_ptp_clock(c);
		}
	}
	free_msg(tx_msg);
#if 1
	if (alloc_msg_cnt)
		printf("alloc: %d\n", alloc_msg_cnt);
#endif
}

void init_stack(u8 *hwaddr)
{
	struct ptp_clock *c;
	u16 ports_left;
	u8 i, j;

	tx_msg_init();
	init_ptp_clocks(hwaddr);

	gptp_get_allowed(&allowed_lost_responses, &allowed_pdelay_faults);

	ports_left = (1 << ptp_ports) - 1;

	/* Create first clock. */
	ptp_clock_cnt = 0;
	ptp_clock_domain_ports[0] = ports_left;
	for (i = 0; i < 3; i++) {
		j = i + 1;
		if (gptp_profile_get_domain(j,
					    &ptp_clock_domains[i],
					    &ptp_clock_domain_ports[i],
					    &ptp_clock_vid[i]))
			continue;
		ptp_clock_domain_ports[i] &= ports_left;
		if (!ptp_clock_domain_ports[i])
			continue;

		c = create_ptp_clock(j, ptp_clock_domains[i],
				     ptp_clock_domain_ports[i],
				     ptp_clock_vid[i]);
		if (c) {
			ports_left &= ~c->ports;
			c->stop = true;
			ptp_clock_ptr[ptp_clock_cnt++] = c;
		}
	}
}

int main(int argc, char *argv[])
{
	char devname[40], filename[80];
	int i, rc;
	char *eth_dev = "eth0";
	u8 hwaddr[8];
	int family = AF_INET;
	mqd_t queue;

	strcpy(devname, "eth0");
	filename[0] = '\0';
	if (argc > 1) {
		i = 1;
		while (i < argc) {
			if ('-' == argv[i][0]) {
				switch (argv[i][1]) {
				case 'f':
					++i;
					if (i >= argc)
						break;
					strncpy(filename, argv[i],
						sizeof(filename) - 1);
					break;
				}
			} else {
				strncpy(devname, argv[i], sizeof(devname) - 1);
			}
			++i;
		}
	}
	if (*filename)
		init_config(filename);
	do {
		u8 major, minor;

		if (!gptp_profile_get_sdoid(1, &major, &minor) && major)
			eth_others[0] = 0;
	} while (0);

	SocketInit(0);

	family = AF_PACKET;
	init_timer_sys();
	init_schedule_sys();
	rc = init_sock(devname, family, hwaddr);
	if (rc) {
		printf("sock error: %d\n", rc);
	}
	init_task();
	if (!ptp_ports)
		ptp_ports = 5;

	ksz_init_timer(&log_timer, "log_timer", 500, log_func, NULL,
		NULL, TRUE);

	do {
		u8 phys_port, virt_port;
		u32 port_mask;

		rc = ptp_port_info(&ptpdev, devname, &phys_port, &port_mask);
#if 0
		if (!rc) {
			printf("%x %x\n", phys_port, port_mask);
		}
#endif
	} while (0);
	init_stack(hwaddr);
	init_aed();
	start_task();
	stack_running = 1;
        ksz_start_timer(&log_timer, log_timer.period);
	exception_log(0, 0, "Started");

	rc = 0;
	do {
		switch (rc) {
		case 1:
			rc = 2;
#ifdef _SYS_SOCKET_H
			if (!ptp_hw)
				break;
#endif
			break;
		default:
			rc = get_ptp_cmd(stdin);
			break;
		}
		if (!rc)
			break;
	} while (1);

	exception_log(0, 0, "Stopped");
#if 0
	exception_log(0, 0, "0123456789012345678901234567890123456789");
#endif
	ksz_stop_timer(&log_timer);
	ksz_exit_timer(&log_timer);
	print_dbg_msg();
	print_log();
	stack_running = 0;
	exit_stack();
	exit_task();
	exit_sock(devname);
	exit_schedule_sys();
	exit_timer_sys();

	SocketExit();

	return 0;
}

