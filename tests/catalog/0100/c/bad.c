float sum_steps(void)
{
    float total = 0.0f;
    for (float step = 0.0f; step < 1.0f; step += 0.1f) {
        total += step;
    }
    return total;
}
