int run(int (*step)(void))
{
    int status = step();
    return (status == 0) ? 0 : 1;   /* main returns the status */
}
