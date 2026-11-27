int third(const int *values)
{
    return values[2];
}

long length(const char *start, const char *end)
{
    return end - start;
}

int sum(const int *values, const int *end)
{
    int total = 0;
    while (values != end) {
        total += *values;
        values++;
    }
    return total;
}
