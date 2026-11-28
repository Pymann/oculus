#include <stdio.h>

FILE *open_config(void)
{
    return fopen("settings.json", "r");
}
