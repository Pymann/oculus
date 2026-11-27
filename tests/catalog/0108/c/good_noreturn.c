#include <stdlib.h>

int checked(int value)
{
    if (value < 0) {
        abort();   /* not a return */
    }
    return value;
}
