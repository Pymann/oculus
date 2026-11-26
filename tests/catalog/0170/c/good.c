#include <stddef.h>

void sort(int *items, size_t count)
{
    for (size_t outer = 1U; outer < count; outer++) {
        int item = items[outer];
        size_t inner = outer;
        while ((inner > 0U) && (items[inner - 1U] > item)) {
            items[inner] = items[inner - 1U];
            inner--;
        }
        items[inner] = item;
    }
}
