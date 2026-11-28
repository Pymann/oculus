struct name {
    char text[16];
};

struct name make_name(void);

char first_letter(void)
{
    struct name copy = make_name();
    return copy.text[0];
}
