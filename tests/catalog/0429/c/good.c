#include <stdarg.h>

char first(int count, ...)
{
    va_list args;
    va_start(args, count);
    char value = (char)va_arg(args, int);
    va_end(args);
    return value;
}
