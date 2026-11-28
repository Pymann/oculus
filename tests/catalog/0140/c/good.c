/* comments and directives may come first */
#define FEATURE_LEVEL 2
#include <stdio.h>

static int counter;

int show(void)
{
    return printf("%d\n", counter);
}
