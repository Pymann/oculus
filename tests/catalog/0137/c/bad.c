int sum(int count)
{
    int values[count];
    int total = 0;
    for (int index = 0; index < count; index++) {
        values[index] = index;
        total += values[index];
    }
    return total;
}
