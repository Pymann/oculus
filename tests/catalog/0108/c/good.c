int sign(int value)
{
    int result = 0;
    if (value < 0) {
        result = -1;
    } else if (value > 0) {
        result = 1;
    } else {
        result = 0;
    }
    return result;
}
