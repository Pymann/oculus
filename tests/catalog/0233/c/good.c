static inline int square_int(int value)
{
    return value * value;
}

#define SQUARE(x) _Generic((x), int: square_int, default: square_int)(x)

int square(int value)
{
    return SQUARE(value);
}
