#define _GNU_SOURCE
#include <unistd.h>

int drop(uid_t user, gid_t group)
{
    if (setgid(group) != 0) {
        return -1;
    }
    return setuid(user);
}
