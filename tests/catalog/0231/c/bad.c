#define IS_POINTER(x) _Generic((x), void *: 1, default: 0)

int check(char *text)
{
    return IS_POINTER(text);
}
