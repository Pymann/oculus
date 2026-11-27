int limit(int value)
{
    if (value > 100) {
        value = 100;
    } else if (value < 0) {
        value = 0;
    } else {
        value = value + 0;
    }
    return value;
}
