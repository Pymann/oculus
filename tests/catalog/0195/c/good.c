typedef void (*handler_t)(int);

void on_error(int code);

handler_t current = &on_error;

void report(void)
{
    on_error(1);
}
