int open_all(int (*open_one)(int), int count)
{
    int result = 0;
    for (int index = 0; (index < count) && (result == 0); index++) {
        if (open_one(index) != 0) {
            result = -1;
        }
    }
    return result;
}
