#define _POSIX_C_SOURCE 200809L
#include <pthread.h>

void *work(void *argument)
{
    int previous;
    (void)pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &previous);
    return argument;
}
