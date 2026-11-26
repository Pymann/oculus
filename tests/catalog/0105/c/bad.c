int retry(int (*attempt)(void))
{
    int tries = 0;
again:
    tries++;
    if ((attempt() != 0) && (tries < 3)) {
        goto again;
    }
    return tries;
}
