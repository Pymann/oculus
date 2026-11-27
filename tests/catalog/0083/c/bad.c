int is_odd(void *pointer)
{
    return (int)(long)pointer & 1;
}
