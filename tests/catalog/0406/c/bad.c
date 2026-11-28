#include <string.h>
#include <threads.h>

static int work(void *argument)
{
    char *word = strtok((char *)argument, " ");
    return (word != NULL) ? 1 : 0;
}

void run(char *text)
{
    thrd_t worker;
    if (thrd_create(&worker, work, text) == thrd_success) {
        (void)thrd_join(worker, NULL);
    }
}
