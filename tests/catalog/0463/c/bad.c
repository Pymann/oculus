#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <signal.h>

void stop(pthread_t worker)
{
    (void)pthread_kill(worker, SIGTERM);
}
