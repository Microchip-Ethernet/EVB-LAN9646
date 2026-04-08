#include "wnp.h"
#include "wrapthread.h"
#include "signalthread.h"


#ifdef _SYS_SOCKET_H
#if 0
struct sockaddr_ll eth_bpdu_addr[NUM_OF_PORTS];
struct sockaddr_ll eth_others_addr[NUM_OF_PORTS];
struct sockaddr_ll eth_ucast_addr[NUM_OF_PORTS];
u8 *eth_bpdu_buf[NUM_OF_PORTS];
u8 *eth_others_buf[NUM_OF_PORTS];
u8 *eth_ucast_buf[NUM_OF_PORTS];

u8 eth_bpdu[] = { 0x01, 0x80, 0xC2, 0x00, 0x00, 0x00 };
u8 eth_others[] = { 0x01, 0x1B, 0x19, 0x00, 0x00, 0x01 };

u8 hw_addr[ETH_ALEN];

pthread_mutex_t disp_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t sec_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t tx_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

void pthread_cond_init_(pthread_cond_t *cond)
{
	pthread_cond_init(cond, NULL);
}

void pthread_mutex_init_(pthread_mutex_t *mutex)
{
	pthread_mutexattr_t attr;

	pthread_mutexattr_init(&attr);
	pthread_mutex_init(mutex, &attr);
}

#if 0
int rx_wait[NUM_OF_PORTS];
struct thread_info rx_thread[NUM_OF_PORTS];
struct thread_info tx_done_thread;
struct thread_info tx_job_thread;
struct thread_info tx_next_thread;
struct thread_info tx_periodic_thread;
#endif

void signal_update(struct thread_info *pthread, int *signal, int val)
{
	Pthread_mutex_lock(&pthread->mutex);
	if (signal)
		*signal = val;
	pthread_cond_signal(&pthread->cond);
	Pthread_mutex_unlock(&pthread->mutex);
}

static void signal_wait_(struct thread_info *pthread, int cond,
	struct timespec *ts)
{
	int n;

	Pthread_mutex_lock(&pthread->mutex);
	if (!cond)
		n = pthread_cond_timedwait(&pthread->cond,
			&pthread->mutex, ts);
	Pthread_mutex_unlock(&pthread->mutex);
}

void signal_long_wait(struct thread_info *pthread, int cond)
{
	struct timespec ts;
	struct timeval tv;
	int n;

	gettimeofday(&tv, NULL);
	ts.tv_sec = tv.tv_sec;
#if 0
	ts.tv_nsec = (tv.tv_usec + 90 * 1000) * 1000;
	if (ts.tv_nsec >= 1000000000) {
		ts.tv_nsec -= 1000000000;
		ts.tv_sec++;
	}
#endif
	ts.tv_nsec = tv.tv_usec * 1000;
	ts.tv_sec += 2;

	signal_wait_(pthread, cond, &ts);
}

void signal_wait(struct thread_info *pthread, int cond)
{
	struct timespec ts;
	struct timeval tv;
	int n;

	gettimeofday(&tv, NULL);
	ts.tv_sec = tv.tv_sec;
	ts.tv_nsec = (tv.tv_usec + 90 * 1000) * 1000;
	if (ts.tv_nsec >= 1000000000) {
		ts.tv_nsec -= 1000000000;
		ts.tv_sec++;
	}

	signal_wait_(pthread, cond, &ts);
}

void signal_init(struct thread_info *pthread)
{
	pthread_cond_init_(&pthread->cond);
	pthread_mutex_init_(&pthread->mutex);
}
#endif


