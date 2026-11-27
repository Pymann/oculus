#include <stdint.h>
#include <string.h>

struct header {
    uint32_t magic;
    uint32_t size;
};

uint32_t as_word(const float *value)
{
    uint32_t word;
    memcpy(&word, value, sizeof word);
    return word;
}

const unsigned char *bytes(const struct header *item)
{
    return (const unsigned char *)item;
}

uint32_t first(struct header *item)
{
    return *(uint32_t *)item;   /* pointer to the first member */
}
