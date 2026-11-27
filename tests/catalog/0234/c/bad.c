#define KIND(x) _Generic((x), int: 1, default: 0, long: 2)

int kind(int value)
{
    return KIND(value);
}
