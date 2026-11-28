#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <signal.h>

int alive(pthread_t worker)
{
    return pthread_kill(worker, 0) == 0;
}

void stop(pthread_t worker)
{
    (void)pthread_cancel(worker);
    (void)pthread_join(worker, NULL);
}
