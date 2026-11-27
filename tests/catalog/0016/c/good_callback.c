typedef void (*handler_t)(int code, void *context);

static void ignore_context(int code, void *context)
{
    (void)context;
    (void)code;
}

handler_t default_handler = ignore_context;
