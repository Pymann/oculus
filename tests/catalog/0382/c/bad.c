int classify(int value)
{
    if ((value < 0) || (value > 100)) {
        return -1;
    }
    return (value > 50) ? 1 : 0;
}
