#include <ctype.h>

int count_letters(const char *text)
{
    int count = 0;
    for (; *text != '\0'; text++) {
        if (isalpha(*text) != 0) {
            count++;
        }
    }
    return count;
}
