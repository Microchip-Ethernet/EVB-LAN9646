#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include "wnp.h"
#include "datatype.h"
#include "os.h"
#include "signalthread.h"
#include "wrapthread.h"
#include "sw_support.h"


void mem_info_init(void)
{
}

void *mem_info_alloc(size_t size)
{
	return malloc(size);
}

void mem_info_free(void *ptr)
{
	free(ptr);
}

/* -------------------------------------------------------------------------- */

typedef struct {
	int fTaskStop;
	void *param;

#if defined(_WIN32)
	HANDLE hevTaskComplete;
#endif
} TTaskParam, *PTTaskParam;

#define MAX_TIMERS  80

static struct ksz_timer_info **Timers;

static struct pollfd *pollfd;
static int pollcnt;

static pthread_t tid[2];
static TTaskParam param[2];

static bool timer_dbg;

void cfg_timer_dbg(bool on)
{
	timer_dbg = on;
}

unsigned long get_sys_time(void)
{
	struct timespec curr;
	u64 nsec;

	clock_gettime(CLOCK_MONOTONIC, &curr);
	nsec = curr.tv_sec;
	nsec *= 1000000000;
	nsec += curr.tv_nsec;
	nsec /= 1000000;
	return (unsigned long)nsec;
}

static int TimerGetIndex(TimerHandle_t xTimer)
{
	int i;

	for (i = 0; i < pollcnt; i++) {
		if (Timers[i]->xTimer == -1)
			continue;
		if (Timers[i]->xTimer == xTimer)
			return i;
	}
	return -1;
}

struct ksz_timer_info *pcTimerGetName(TimerHandle_t xTimer)
{
	int i = TimerGetIndex(xTimer);

	if (i >= 0)
		return Timers[i];
	return NULL;
}

void ksz_exit_timer(struct ksz_timer_info *info)
{
	close(info->xTimer);
	info->xTimer = -1;
	info->callback = NULL;
}

void ksz_init_timer(struct ksz_timer_info *info, char *name, TickType_t xTicks,
	void (*callback)(TimerHandle_t xTimer), void (*func)(void *param),
	void *param, bool repeat)
{
	struct pollfd *dest = pollfd;

	snprintf(info->name, 40, "%s", name);
	info->period = xTicks;
	info->repeat = repeat;
	info->callback = callback;
	info->func = func;
	info->param = param;
	info->xTimer = timerfd_create(CLOCK_MONOTONIC, 0);

	info->index = pollcnt++;
	Timers[info->index] = info;
	dest[info->index].fd = info->xTimer;
	dest[info->index].events = POLLIN|POLLPRI;
#if 1
	info->dbg = timer_dbg;
#if 0
printf("init_timer: %d %s\n", info->index, info->name);
#endif
#endif
}

void convert_ticks_to_ns(TickType_t xTicks, int *seconds, int *nanoseconds)
{
	*seconds = 0;
	if (xTicks >= 1000) {
		*seconds = xTicks / 1000;
		xTicks %= 1000;
	}
	*nanoseconds = xTicks * 1000000;
}

void ksz_change_timer(struct ksz_timer_info *info, TickType_t next,
	TickType_t xTicks)
{
	struct itimerspec tmo = {
		{0, 0}, {0, 0}
	};
	int seconds, nanoseconds;

#if 0
printf("c: %lu %lu\n", next, xTicks);
#endif
	convert_ticks_to_ns(next, &seconds, &nanoseconds);
	tmo.it_value.tv_sec = seconds;
	tmo.it_value.tv_nsec = nanoseconds;
	if (info->repeat) {
		convert_ticks_to_ns(xTicks, &seconds, &nanoseconds);
		tmo.it_interval.tv_sec = seconds;
		tmo.it_interval.tv_nsec = nanoseconds;
	}
	timerfd_settime(info->xTimer, 0, &tmo, NULL);
}

void ksz_start_timer(struct ksz_timer_info *info, TickType_t xTicks)
{
	struct pollfd *dest = pollfd;
	struct itimerspec tmo = {
		{0, 0}, {0, 0}
	};
	int seconds, nanoseconds;

#if 1
	if (info->dbg)
printf("start timer: %d %s %ld\n", info->index, info->name, xTicks);
#endif
	convert_ticks_to_ns(xTicks, &seconds, &nanoseconds);
	tmo.it_value.tv_sec = seconds;
	tmo.it_value.tv_nsec = nanoseconds;
	if (info->repeat) {
		xTicks = info->period;
		convert_ticks_to_ns(xTicks, &seconds, &nanoseconds);
		tmo.it_interval.tv_sec = seconds;
		tmo.it_interval.tv_nsec = nanoseconds;
	}
	timerfd_settime(info->xTimer, 0, &tmo, NULL);
	info->max = info->period;

	dest[info->index].fd = info->xTimer;
}

void ksz_stop_timer(struct ksz_timer_info *info)
{
	struct pollfd *dest = pollfd;
	struct itimerspec tmo = {
		{0, 0}, {0, 0}
	};

	if (!info->max)
		return;
	timerfd_settime(info->xTimer, 0, &tmo, NULL);
	info->max = 0;

	dest[info->index].fd = -1;
}

#ifdef _SYS_SOCKET_H
void *

#else
void
#endif
TimerTask(void *param)
{
	volatile PTTaskParam pTaskParam;
	struct ksz_timer_info *info;
	struct pollfd *cur;
	int cnt, i, rc;
	char dummy[8];

	pTaskParam = (PTTaskParam) param;
	FOREVER {
		if ( pTaskParam->fTaskStop ) {
			break;
		}
		cnt = poll(pollfd, pollcnt, 1000);
		if (cnt < 0) {
			break;
		} else if (!cnt) {
			continue;
		}

		cur = pollfd;
		for (i = 0; i < pollcnt; i++) {
			/* When closing. */
			if (cur[i].revents & POLLNVAL)
				continue;
			if (cur[i].revents & (POLLIN|POLLPRI)) {
				/* Clear the indication. */
				rc = read(cur[i].fd, dummy, 8);
				info = Timers[i];
				if (info->callback) {
					info->callback(info->xTimer);
				}
			}
		}
	}
	pTaskParam->fTaskStop = TRUE;

#ifdef _WIN32
	SetEvent( pTaskParam->hevTaskComplete );
#endif

#ifdef _SYS_SOCKET_H
	return NULL;
#endif
}  /* TimerTask */


void exit_timer_sys(void)
{
	void *status;

	param[0].fTaskStop = TRUE;
	Pthread_join(tid[0], &status);

	free(Timers);
	free(pollfd);
}

void init_timer_sys(void)
{
	Timers = calloc(sizeof(void*), MAX_TIMERS);
	pollfd = calloc(sizeof(struct pollfd), MAX_TIMERS);

	param[0].fTaskStop = FALSE;
	param[0].param = NULL;

	Pthread_create(&tid[0], NULL, TimerTask, &param[0]);
}

/* -------------------------------------------------------------------------- */

static struct ksz_schedule_info schedule_info;

void init_work(struct ksz_schedule_work *work, void *dev,
	void (*func)(struct ksz_schedule_work *work))
{
	work->dev = dev;
	work->func = func;
	work->next = NULL;
	work->attach = false;
	mutex_init(&work->lock);
}

static void add_work(struct ksz_schedule_info *info,
	struct ksz_schedule_work *work)
{
	/* Not already scheduled. */
	if (!work->attach) {
		/* Assume not delayed. */
		work->xTicks = 0;
		work->next = NULL;
		info->last->next = work;
		info->last = work;
		work->attach = true;
	}
}

static void schedule_work_to_task(struct ksz_schedule_info *info,
	struct ksz_schedule_work *work)
{
	mutex_lock(&info->lock);
	add_work(info, work);
	mutex_unlock(&info->lock);
	if (work->attach)
		signal_update(&info->thread, NULL, 0);
}

void schedule_work(struct ksz_schedule_work *work)
{
	schedule_work_to_task(&schedule_info, work);
}

void schedule_delayed_work(struct ksz_schedule_work *work, TickType_t xTicks)
{
	struct ksz_schedule_info *info = &schedule_info;
	struct ksz_timer_info *timer = &schedule_info.timer;

	mutex_lock(&info->lock);
	add_work(info, work);

	/* Indicate delayed work. */
	work->xTicks = jiffies + xTicks;
	mutex_unlock(&info->lock);

	/* Start timer if not running. */
	if (!timer->max)
		ksz_start_timer(timer, timer->period);
}

static void delayed_timer_func(TimerHandle_t xTimer)
{
	struct ksz_timer_info *timer = (struct ksz_timer_info *)
		pcTimerGetName(xTimer);
	struct ksz_schedule_info *info = timer->param;

	if (info->pause)
		return;
	signal_update(&info->thread, NULL, 0);
}

void schedule_pause(void)
{
	struct ksz_schedule_info *info = &schedule_info;

	info->pause = 1;
}

void schedule_resume(void)
{
	struct ksz_schedule_info *info = &schedule_info;

	info->pause = 0;
}

#ifdef _SYS_SOCKET_H
void *

#else
void
#endif
ScheduleTask(void *param)
{
	volatile PTTaskParam pTaskParam;
	struct ksz_schedule_info *info;
	struct ksz_schedule_work *prev;
	struct ksz_schedule_work *work;

	pTaskParam = (PTTaskParam) param;
	info = pTaskParam->param;
	FOREVER {
		if ( pTaskParam->fTaskStop ) {
			break;
		}
		signal_long_wait(&info->thread, FALSE);

		prev = &info->anchor;
		work = prev->next;
		while (work) {
			mutex_lock(&info->lock);

			/* This is a delayed task and it is not ready to run. */
			if (work->xTicks &&
			    !time_after_eq(jiffies, work->xTicks)) {
				prev = work;
				work = prev->next;
				mutex_unlock(&info->lock);
				continue;
			}
			prev->next = work->next;
			work->next = NULL;
			work->attach = false;
			if (info->last == work) {
				info->last = prev;

				/* No more delay task to run. */
				if (info->last == &info->anchor && work->xTicks)
					ksz_stop_timer(&info->timer);
			}
			mutex_unlock(&info->lock);
			mutex_lock(&work->lock);
			work->func(work);
			mutex_unlock(&work->lock);
			work = prev->next;
		}
	}
	pTaskParam->fTaskStop = TRUE;

#ifdef _WIN32
	SetEvent( pTaskParam->hevTaskComplete );
#endif

#ifdef _SYS_SOCKET_H
	return NULL;
#endif
}

void exit_schedule_sys(void)
{
	unsigned long tick = get_sys_time();
	void *status;

printf("wait sched %lu\n", tick);

	param[1].fTaskStop = TRUE;
	Pthread_join(tid[1], &status);

	ksz_stop_timer(&schedule_info.timer);
	ksz_exit_timer(&schedule_info.timer);
	tick = get_sys_time();
printf("exit %lu\n", tick);
}

void init_schedule_sys(void)
{
	struct ksz_schedule_info *info;
	struct ksz_timer_info *timer;

	info = &schedule_info;
	timer = &schedule_info.timer;

	mutex_init(&info->lock);
	info->anchor.next = NULL;
	info->last = &info->anchor;

	signal_init(&info->thread);
	ksz_init_timer(timer, "delayed_timer", msecs_to_jiffies(1),
		delayed_timer_func, NULL, info, TRUE);

	param[1].fTaskStop = FALSE;
	param[1].param = info;

	Pthread_create(&tid[1], NULL, ScheduleTask, &param[1]);
}


