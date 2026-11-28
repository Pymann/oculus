int sum(int *items, int count)
{
    int total = 0;
    for (int index = 0; index < count; index++) {
        total += items[index];
    }
    return total;
}
