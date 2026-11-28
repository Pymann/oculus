#define KIND(x) _Generic((x), int: 2, long: 3, default: 0)

int kind(int value)
{
    return KIND(value);
}
