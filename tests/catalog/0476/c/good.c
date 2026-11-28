#include <stdlib.h>
#include <wchar.h>

size_t length(const wchar_t *text)
{
    return wcslen(text);
}

size_t to_wide(wchar_t *target, size_t count, const char *source)
{
    return mbstowcs(target, source, count);
}
