#include <stdatomic.h>

struct pair { int first; int second; };

_Atomic struct pair shared;

int first_of(void)
{
    struct pair copy = atomic_load(&shared);
    return copy.first;
}
