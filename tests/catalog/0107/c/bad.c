int find(const int *items, int count, int wanted)
{
    int index = 0;
    while (index < count) {
        if (items[index] == wanted) {
            break;
        }
        if (items[index] < 0) {
            break;
        }
        index++;
    }
    return index;
}
