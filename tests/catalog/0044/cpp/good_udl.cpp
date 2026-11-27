constexpr unsigned long long operator""_km(unsigned long long value)
{
    return value * 1000ULL;
}

unsigned long long distance = 10_km;
