#define COMPLICATED(value) ((value) * 3 + ((value) << 2) - ((value) >> 1))

int sum(int left, int right)
{
    /* comments do not count, however long they are */
    return COMPLICATED(left) + right;
}
