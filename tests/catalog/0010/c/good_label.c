int find(const int *items, int count, int wanted)
{
    int index = 0;
    while (index < count) {
        if (items[index] == wanted) {
            goto found;
        }
        index++;
    }
    return -1;
found:
    return index;
}
