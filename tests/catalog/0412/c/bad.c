#include <stdatomic.h>
#include <stdbool.h>

atomic_bool flag;

void toggle(void)
{
    flag = !flag;
}
