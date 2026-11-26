int lookup(int index)
{
    static const int table[3] = { 10, 20, 30 };
    return table[index];
}

const char *day_name(int day)
{
    /* the pointers are not const, but nothing writes them: a lookup table */
    static const char *names[2] = { "monday", "tuesday" };
    return names[day];
}
