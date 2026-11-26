int pick(int flag)
{
    int value = 0;
    if (flag != 0) {
        goto done;
    }
    value = 1;
done:
    return value + 1;
}
