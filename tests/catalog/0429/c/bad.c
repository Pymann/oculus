#include <stdarg.h>

char first(int count, ...)
{
    va_list args;
    va_start(args, count);
    char value = va_arg(args, char);
    va_end(args);
    return value;
}
