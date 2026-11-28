#include <string.h>
#include <wchar.h>

size_t length(const wchar_t *text)
{
    return strlen((const char *)text);
}
