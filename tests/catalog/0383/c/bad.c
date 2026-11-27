int count(const int *items, int size)
{
    int total = 0;
    for (int outer = 0; outer < size; outer++) {
        if (items[outer] > 0) {
            while (total < items[outer]) {
                total++;
            }
        }
    }
    return total;
}
