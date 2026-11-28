#define IS_TEXT(x) _Generic((x), char *: 1, const char *: 1, default: 0)

int check(char *text)
{
    return IS_TEXT(text);
}
