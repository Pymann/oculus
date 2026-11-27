#define IS_INT(x) _Generic((x), int: 1, default: 0)

int check(int value)
{
    return IS_INT(value);
}
