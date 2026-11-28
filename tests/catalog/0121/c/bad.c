unsigned int factorial(unsigned int value)
{
    if (value <= 1U) {
        return 1U;
    }
    return value * factorial(value - 1U);
}
