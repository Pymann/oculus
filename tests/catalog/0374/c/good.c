#include <locale.h>
#include <stddef.h>

const char *current_locale(void)
{
    return setlocale(LC_ALL, NULL);
}
