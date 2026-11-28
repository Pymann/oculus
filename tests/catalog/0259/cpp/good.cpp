int sum_positive(const int *items, int count)
{
    int sum = 0;
    for (int index = 0; index < count; index++) {
        if (items[index] < 0) {
            continue;
        }
        sum += items[index];
    }
    return sum;
}
