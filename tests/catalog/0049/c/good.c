#include "good_scale.h"

int scale(int value)
{
    return value * 2;
}

static int helper(void)
{
    return 1;
}

int main(void)
{
    return helper() + scale(0);
}
