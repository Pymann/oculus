struct box {
    int values[2];
};

struct box make(void);

void touch(void)
{
    make().values[0] = 1;
}
