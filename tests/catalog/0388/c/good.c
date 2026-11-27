/* Scales a value by three and adds one. */
int scale(int value)
{
    int result = value * 3;   /* never overflows for inputs below 1000 */
    return result + 1;
}
