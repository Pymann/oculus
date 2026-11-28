#include <stdlib.h>
#include <string.h>

struct packet { size_t size; unsigned char data[]; };

struct packet *make_packet(size_t size)
{
    struct packet *result = malloc(sizeof *result + size);
    if (result != NULL) {
        result->size = size;
        (void)memset(result->data, 0, size);
    }
    return result;
}
