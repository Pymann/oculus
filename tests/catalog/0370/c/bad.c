#include <errno.h>

int open_device(int number)
{
    if (number < 0) {
        errno = EINVAL;
        return -1;
    }
    return number;
}
