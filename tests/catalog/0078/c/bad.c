typedef void (*callback_t)(int);

void handler(long code);

callback_t pick(void)
{
    return (callback_t)handler;
}
