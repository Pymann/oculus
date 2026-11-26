#include <stdlib.h>

struct packet {
    size_t length;
    unsigned char data[1];
};

unsigned char last(const struct packet *packet)
{
    return packet->data[packet->length - 1U];
}
