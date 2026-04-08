
#ifndef _SW_SUPPORT_H
#define _SW_SUPPORT_H

#include "signalthread.h"


void mem_info_init(void);
void *mem_info_alloc(size_t size);
void mem_info_free(void *ptr);


struct ksz_timer_info {
    /* Put in front so that this structure can be substitued for char*. */
    char name[40];
#if( configSUPPORT_STATIC_ALLOCATION == 1 )
    StaticTimer_t xTimerBuffer;
#endif
    TimerHandle_t xTimer;
    void (*callback)(TimerHandle_t xTimer);
    void (*func)(void *param);
    void *param;
#if 0
    int set;
#endif
    bool dbg;
    bool repeat;
    TickType_t max;
    TickType_t period;
    int index;
};

void cfg_timer_dbg(bool on);

unsigned long get_sys_time(void);

struct ksz_timer_info *pcTimerGetName(TimerHandle_t xTimer);

void ksz_exit_timer(struct ksz_timer_info *info);
void ksz_init_timer(struct ksz_timer_info *info, char *name, TickType_t xTicks,
    void (*callback)(TimerHandle_t xTimer), void (*func)(void *param),
    void *param, bool repeat);
void ksz_change_timer(struct ksz_timer_info *info, TickType_t next,
    TickType_t xTicks);
void ksz_start_timer(struct ksz_timer_info *info, TickType_t xTicks);
void ksz_stop_timer(struct ksz_timer_info *info);

void exit_timer_sys(void);
void init_timer_sys(void);


struct ksz_schedule_work {
    void (*func)(struct ksz_schedule_work *work);
    void *dev;
    TickType_t xTicks;
    bool attach;
#if 0
    struct mutex lock;
#endif
    pthread_mutex_t lock;
    struct ksz_schedule_work *next;
#ifdef DEBUG_SCHEDULE_WORK
    char name[20];
#endif
};

void init_work(struct ksz_schedule_work *work, void *dev,
    void (*func)(struct ksz_schedule_work *work));
void schedule_delayed_work(struct ksz_schedule_work *work, TickType_t xTicks);
void schedule_work(struct ksz_schedule_work *work);
void schedule_pause(void);
void schedule_resume(void);

struct ksz_schedule_info {
#if 0
    TaskHandle_t xScheduleTask;
#endif
    struct thread_info thread;
    struct ksz_timer_info timer;
    bool low_prio;
    bool pause;
#if 0
    struct mutex lock;
#endif
    pthread_mutex_t lock;
    struct ksz_schedule_work anchor;
    struct ksz_schedule_work *last;
};

void exit_schedule_sys(void);
void init_schedule_sys(void);

#if 0
struct sw_timer_priv {
#if 0
    TaskHandle_t xTimerTask;
#endif
    struct ksz_schedule_info delayed;
    struct ksz_schedule_info low_prio;
    struct ksz_schedule_info schedule;
#if 0
    struct ksz_timer_info mib_timer_info;
    struct ksz_timer_info monitor_timer_info;
#endif
};
#endif

#endif

