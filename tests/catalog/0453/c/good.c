#include <assert.h>

static void check(int condition)
{
    assert(condition);
}

typedef void (*check_t)(int);

check_t checker = check;
