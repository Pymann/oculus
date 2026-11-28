#include <stdlib.h>

int sign(int value)
{
    if (value > 0) {
        return 1;
    } else if (value < 0) {
        return -1;
    } else {
        return 0;
    }
}

int checked(int value)
{
    if (value >= 0) {
        return value;
    }
    abort();
}
