#define IN_RANGE(value) (((value) >= 0) && ((value) <= 100))

int classify(int value)
{
    if (!IN_RANGE(value)) {   /* the macro is not expanded: counts as 1 */
        return -1;
    }
    return (value > 50) ? 1 : 0;
}
