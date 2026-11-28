#include <string.h>
#include <threads.h>

static int work(void *argument)
{
    const char *text = argument;
    return (strchr(text, ' ') != NULL) ? 1 : 0;
}

void run(char *text)
{
    thrd_t worker;
    if (thrd_create(&worker, work, text) == thrd_success) {
        (void)thrd_join(worker, NULL);
    }
}
