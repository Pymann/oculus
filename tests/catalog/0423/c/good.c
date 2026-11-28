struct box {
    int values[2];
};

struct box make(void);

int touch(void)
{
    struct box copy = make();
    copy.values[0] = 1;
    return copy.values[0];
}
