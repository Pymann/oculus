static int *last_value;

int *make(void)
{
    int value = 5;
    last_value = &value;
    return last_value;
}
