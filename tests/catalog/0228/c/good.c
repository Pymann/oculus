int pick(const int *counter)
{
    return _Generic(*counter, int: 1, default: 0);
}
