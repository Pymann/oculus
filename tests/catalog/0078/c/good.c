#include <stddef.h>

typedef void (*callback_t)(int);

void handler(int code);

callback_t pick(int enabled)
{
    callback_t none = NULL;
    return (enabled != 0) ? handler : none;
}
