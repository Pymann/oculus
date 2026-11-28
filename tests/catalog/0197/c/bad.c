struct name {
    char text[16];
};

struct name make_name(void);

const char *first_name(void)
{
    return make_name().text;
}
