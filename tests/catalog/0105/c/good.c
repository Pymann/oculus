int cleanup_on_error(int (*step)(int))
{
    int result = step(1);
    if (result != 0) {
        goto done;
    }
    result = step(2);
done:
    return result;
}
