unsigned int factorial(unsigned int value)
{
    unsigned int result = 1U;
    for (unsigned int factor = 2U; factor <= value; factor++) {
        result *= factor;
    }
    return result;
}
