#include <stdio.h>

void show(long count, const char *name, short level, float ratio)
{
    (void)printf("%ld items in %s\n", count, name);
    (void)printf("%d %f\n", level, ratio);   /* promoted to int and double */
    (void)printf("100%%\n");
}
