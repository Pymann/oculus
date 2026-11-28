#include <string.h>

void shift_left(char *text)
{
    memcpy(text, text + 1, strlen(text));
}
