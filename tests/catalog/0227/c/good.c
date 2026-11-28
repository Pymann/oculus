#define TYPE_WIDTH(x) _Generic((x), int: 4, long: 8, default: 0)

int width(int value)
{
    return TYPE_WIDTH(value);
}
