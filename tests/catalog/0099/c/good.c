unsigned long size_of_next(int *counter)
{
    unsigned long size = (unsigned long)sizeof(*counter);
    (*counter)++;
    return size;
}
