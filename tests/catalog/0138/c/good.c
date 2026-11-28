#include <string.h>

void shift_left(char *text)
{
    memmove(text, text + 1, strlen(text));
}
