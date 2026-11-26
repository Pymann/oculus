int pick(int flag)
{
    int value = 0;
    if (flag != 0) {
        goto inside;
    }
    {
        value = 1;
inside:
        value = value + 1;
    }
    return value;
}
