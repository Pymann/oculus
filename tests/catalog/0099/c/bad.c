unsigned long size_of_next(int *counter)
{
    return (unsigned long)sizeof((*counter)++);
}
