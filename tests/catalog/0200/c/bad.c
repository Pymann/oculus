#include <string.h>

int is_admin(const char name[16])
{
    return memcmp(name, "admin", 6U) == 0;
}
