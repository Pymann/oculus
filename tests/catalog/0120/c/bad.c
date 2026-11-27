#include <stdarg.h>

int sum(int count, ...)
{
    va_list args;
    int total = 0;
    va_start(args, count);
    for (int index = 0; index < count; index++) {
        total += va_arg(args, int);
    }
    va_end(args);
    return total;
}
