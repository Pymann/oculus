#define KIND(x) _Generic((x), const int: 1, int: 2, default: 0)

int kind(int value)
{
    return KIND(value);
}
