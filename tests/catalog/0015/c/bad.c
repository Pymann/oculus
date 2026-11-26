int first_positive(const int *items, int count)
{
    for (int index = 0; index < count; index++) {
        if (items[index] > 0) {
            return items[index];
        }
    }
done:
    return 0;
}
