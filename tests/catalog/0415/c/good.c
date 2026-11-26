#include <stddef.h>

struct packet {
    size_t length;
    unsigned char data[];
};

struct pair {
    int values[1];   /* fixed array, only element 0 is used */
};

int first(const struct pair *pair)
{
    return pair->values[0];
}

/* a configured size that happens to be 1 in this build: not the old trick */
#define RING_SLOTS 1

struct ring {
    unsigned int count;
    int slots[RING_SLOTS];
};

int slot(const struct ring *ring, unsigned int index)
{
    return ring->slots[index % RING_SLOTS];
}
