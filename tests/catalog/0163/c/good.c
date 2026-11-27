int guarded(int (*step)(void))
{
    int status = step();
    return (status == 0) ? 0 : -1;
}
