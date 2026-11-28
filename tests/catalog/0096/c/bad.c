int sum(const int *values, int count)
{
    int total = 0;
    int index = 0;
    while (index < count) {
        total += values[index++];
    }
    return total;
}
