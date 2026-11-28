#include <ctype.h>
#include <stdio.h>

int count_letters(const char *text)
{
    int count = 0;
    for (; *text != '\0'; text++) {
        if (isalpha((unsigned char)*text) != 0) {
            count++;
        }
    }
    return count;
}

int next_is_digit(FILE *stream)
{
    int item = getc(stream);   /* EOF or an unsigned char value */
    return isdigit(item);
}

int is_space_constant(void)
{
    return isspace(' ');
}
