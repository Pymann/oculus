int average(int sum, int count)
{
    if (count == 0) {
        return 0;
    }
    return sum / count;
}

double ratio(double top, double bottom)
{
    return top / bottom;   /* floating point: not reported by default */
}
