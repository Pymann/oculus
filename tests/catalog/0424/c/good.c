#include <stdalign.h>
#include <stdint.h>
#include <string.h>

uint32_t read_word(const unsigned char *buffer)
{
    uint32_t word;
    memcpy(&word, buffer + 1, sizeof word);
    return word;
}

alignas(uint32_t) static unsigned char storage[16];

uint32_t *as_words(void)
{
    return (uint32_t *)storage;   /* storage is aligned for uint32_t */
}
