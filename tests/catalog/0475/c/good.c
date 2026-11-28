unsigned int histogram_index(const char *text)
{
    return (unsigned int)(unsigned char)text[0];
}

int is_space(const char *text)
{
    return text[0] == ' ';
}
