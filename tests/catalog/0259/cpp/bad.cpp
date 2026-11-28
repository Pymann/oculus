int sum_positive(const int *items, int count)
{
    int sum = 0;
    int index = 0;
    while (index < count) {
        if (items[index] < 0) {
            index++;
            continue;
        }
        sum += items[index];
        index++;
    }
    return sum;
}
