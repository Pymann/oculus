#include <errno.h>
#include <limits.h>
#include <stdlib.h>

int port(const char *text, int *value)
{
    char *end = NULL;
    errno = 0;
    long parsed = strtol(text, &end, 10);
    if ((errno != 0) || (end == text) || (*end != '\0') || (parsed < 0) || (parsed > 65535)) {
        return -1;
    }
    *value = (int)parsed;
    return 0;
}
