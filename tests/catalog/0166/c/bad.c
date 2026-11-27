#include <fenv.h>

void round_down(void)
{
    (void)fesetround(FE_DOWNWARD);
}
