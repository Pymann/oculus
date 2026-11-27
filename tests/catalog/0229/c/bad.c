#define ANY(x) _Generic((x), default: 1)

int any(int value)
{
    return ANY(value);
}
