int read_byte(void);

int skip_spaces(void)
{
    int next;
    while ((next = read_byte()) == ' ') {
    }
    return next;
}
