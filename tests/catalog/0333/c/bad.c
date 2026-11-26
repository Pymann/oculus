int next_id(void)
{
    static int counter = 0;
    counter++;
    return counter;
}
