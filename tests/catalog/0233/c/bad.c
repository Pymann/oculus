#define SQUARE(x) _Generic((x), int: (x) * (x), default: 0)

int square(int value)
{
    return SQUARE(value);
}
