#include <assert.h>
#include <stdlib.h>

_Static_assert(sizeof(int) >= 4, "int has at least 32 bits");

void unreachable_case(int state)
{
    assert(state > 0);
    if (state <= 0) {
        abort();
    }
}
