unsigned long length(const int items[16])
{
    return (unsigned long)(sizeof(items) / sizeof(items[0]));
}
