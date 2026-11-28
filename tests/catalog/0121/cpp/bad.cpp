bool is_even(unsigned int value);

bool is_odd(unsigned int value)
{
    return (value == 0U) ? false : is_even(value - 1U);
}

bool is_even(unsigned int value)
{
    return (value == 0U) ? true : is_odd(value - 1U);
}
