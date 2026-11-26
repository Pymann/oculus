int checked_read(const int *item)
{
#ifdef CHECK_NULL
    if (item == 0) {
        goto fail;
    }
#endif
    return *item;
fail:
    return -1;
}
