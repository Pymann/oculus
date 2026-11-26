int first_positive(const int *items, int count)
{
    int result = 0;
    for (int index = 0; index < count; index++) {
        if (items[index] > 0) {
            result = items[index];
            goto done;
        }
    }
done:
    return result;
}
