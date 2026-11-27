int count_chars(const char *text)
{
    int count = 0;
    while (*text != '\0') {
        count++;
        text++;
    }
    return count;
}
