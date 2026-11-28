#include <stdlib.h>
#include <string.h>

void upper_home(char *buffer, size_t size)
{
    const char *home = getenv("HOME");
    if ((home != NULL) && (strlen(home) < size)) {
        (void)strcpy(buffer, home);
        buffer[0] = 'H';
    }
}
