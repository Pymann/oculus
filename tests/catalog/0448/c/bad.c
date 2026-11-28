#include <stddef.h>

struct packet { size_t size; unsigned char data[]; };

size_t size_of(void)
{
    struct packet local = { 0U };
    return local.size;
}
