#define KIND(x) _Generic((x), int: 1, long: 2, default: 0)

int kind(int value)
{
    return KIND(value);
}
