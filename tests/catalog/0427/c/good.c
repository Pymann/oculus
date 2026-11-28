static int limit = 10;

void raise_limit(const int *view)
{
    int *writable = (int *)view;   /* view points to limit, which is not const */
    *writable = 20;
}

void call(void)
{
    raise_limit(&limit);
}
