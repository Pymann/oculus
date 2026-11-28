#include <stdlib.h>
#include <time.h>

void init_dice(void)
{
    srand((unsigned int)time(NULL));
}

int roll(void)
{
    return (rand() % 6) + 1;
}
