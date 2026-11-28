void clear(int *items, int count);

int sum(const int *items, int count)
{
    int total = 0;
    for (int index = 0; index < count; index++) {
        total += items[index];
    }
    return total;
}

void reset(int *items, int count)
{
    clear(items, count);   /* passed on to a function that modifies */
}
