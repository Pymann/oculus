#include <stdlib.h>

int roll(void)
{
    srand(42U);
    return (rand() % 6) + 1;
}
