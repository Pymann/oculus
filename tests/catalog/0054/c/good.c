int fill(void)
{
    int scratch[16];
    int sum = 0;
    for (int index = 0; index < 16; index++) {
        scratch[index] = index;
        sum += scratch[index];
    }
    return sum;
}
