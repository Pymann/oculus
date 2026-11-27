float sum_steps(void)
{
    float total = 0.0f;
    for (int index = 0; index < 10; index++) {
        total += (float)index * 0.1f;
    }
    return total;
}
