#include <string.h>

int is_admin(const char *name)
{
    return strcmp(name, "admin") == 0;
}

int same_header(const unsigned char left[4], const unsigned char right[4])
{
    return memcmp(left, right, 4U) == 0;   /* raw bytes, not strings */
}
