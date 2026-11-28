#include <stdlib.h>

void upper_home(void)
{
    char *home = getenv("HOME");
    if (home != NULL) {
        home[0] = 'H';
    }
}
