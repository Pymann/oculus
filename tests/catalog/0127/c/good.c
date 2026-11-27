int count_chars(const char *text)
{
    int count = 0;
    for (const char *cursor = text; *cursor != '\0'; cursor++) {
        count++;
    }
    return count;
}

void clear(int *target)
{
    *target = 0;   /* modifies the pointed-to object, not the parameter */
}
