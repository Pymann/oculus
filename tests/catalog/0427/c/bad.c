static const int limit = 10;

void raise_limit(void)
{
    int *writable = (int *)&limit;
    *writable = 20;
}
