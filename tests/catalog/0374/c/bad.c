#include <locale.h>

void use_german(void)
{
    (void)setlocale(LC_ALL, "de_DE.UTF-8");
}
