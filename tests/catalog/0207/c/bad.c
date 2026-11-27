#include <stdlib.h>

int list_files(void)
{
    return system("ls -l");
}
