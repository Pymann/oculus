// expect-compile-error
#include <stdatomic.h>

struct pair { int first; int second; };

_Atomic struct pair shared;

int first_of(void)
{
    return shared.first;
}
