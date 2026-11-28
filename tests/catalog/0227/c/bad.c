int width(int value)
{
    return _Generic(value, int: 4, default: 0);
}
