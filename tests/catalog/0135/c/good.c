void print_value(const int *value);

int *make(void)
{
    static int value = 5;
    int local = 3;
    print_value(&local);   /* used during the call only */
    return &value;
}
