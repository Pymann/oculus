int step(int *first, int *second)
{
    int values[2] = { 1, 2 };
    *first += values[0];
    *second += values[1];
    return *second;
}
