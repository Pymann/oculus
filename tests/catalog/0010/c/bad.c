int clamp(int value)
{
    if (value > 100) {
        return 100;
        value = 100;
    }
    return value;
}
