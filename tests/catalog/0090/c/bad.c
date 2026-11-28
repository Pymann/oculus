unsigned int flags(unsigned int value, unsigned int mask, int ready, int busy)
{
    unsigned int shifted = value << 2U | mask;
    int state = ready && busy || !ready;
    return shifted + (unsigned int)state;
}
