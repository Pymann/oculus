#include <stdio.h>

int first_of_each(FILE **streams)
{
    int item = getc(*streams++);
    return item;
}
