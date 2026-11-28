unsigned int flags(unsigned int value, unsigned int mask, int ready, int busy)
{
    unsigned int shifted = (value << 2U) | mask;
    int state = (ready && busy) || (!ready);
    int sum = ready + busy * 2;
    int ordered = (ready > 0) && (busy < 3);
    return shifted + (unsigned int)(state + sum + ordered);
}
