int total(const int *items, int count)
{
    int sum = 0;
    for (int index = 0; index < count; index++) {
        sum += items[index];
    }
    return sum;
}
