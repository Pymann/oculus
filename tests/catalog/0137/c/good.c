enum { max_count = 64 };

int sum(int count)
{
    int values[max_count];
    int total = 0;
    for (int index = 0; (index < count) && (index < max_count); index++) {
        values[index] = index;
        total += values[index];
    }
    return total;
}
