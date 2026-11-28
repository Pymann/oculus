#include <string.h>

void label(char *buffer, int has_buffer)
{
    const char *text = "none";
    if (has_buffer) {
        strcpy(buffer, text);   /* writes to the buffer, not to the literal */
    }
}
