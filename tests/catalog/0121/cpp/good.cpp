constexpr unsigned int factorial(unsigned int value)
{
    return (value <= 1U) ? 1U : value * factorial(value - 1U);
}

static_assert(factorial(5U) == 120U, "compile time only");
