// expect-compile-error
#include <assert.h>

typedef void (*check_t)(int);

check_t checker = assert;
