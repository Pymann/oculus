#include <stddef.h>

struct handle;

void release(void *memory);

void destroy(struct handle *item)
{
    struct handle *none = NULL;
    release(item);   /* to void *: fine */
    (void)none;
}
