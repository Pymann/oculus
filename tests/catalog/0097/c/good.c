int read_byte(void);

int skip_spaces(void)
{
    int next = read_byte();
    while (next == ' ') {
        next = read_byte();
    }
    return next;
}
