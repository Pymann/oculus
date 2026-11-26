int open_all(int (*open_one)(int), int count)
{
    for (int index = 0; index < count; index++) {
        if (open_one(index) != 0) {
            goto failed;
        }
    }
    return 0;
failed:
    return -1;
}
