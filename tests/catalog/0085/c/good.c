void clear(int *const value)
{
    int *copy = (int *)value;   /* only the top-level const of the pointer is dropped */
    *copy = 0;
}
