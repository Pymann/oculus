#define _GNU_SOURCE
#include <stdlib.h>
#include <unistd.h>

void drop(uid_t user, gid_t group)
{
    if ((setgid(group) != 0) || (setuid(user) != 0)) {
        abort();
    }
}
