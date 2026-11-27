unsigned long length(const int items[16])
{
    (void)items;
    return 16UL;
}

unsigned long local(void)
{
    int items[16] = { 0 };
    return (unsigned long)(sizeof items / sizeof items[0]);
}
