
#ifndef _SIGNALTHREAD_H
#define _SIGNALTHREAD_H

struct thread_info {
	pthread_cond_t  cond;
	pthread_mutex_t mutex;
};

void signal_init(struct thread_info *pthread);
void signal_wait(struct thread_info *pthread, int cond);
void signal_long_wait(struct thread_info *pthread, int cond);
void signal_update(struct thread_info *pthread, int *signal, int val);

#endif

