int count_ones(const int *items, int count)
{
    int ones = 0;
    for (int index = 0; index < count; index++) {
        switch (items[index]) {
        case 1:
            ones++;
            break;          /* leaves the switch, not the loop */
        default:
            break;
        }
    }
    return ones;
}
