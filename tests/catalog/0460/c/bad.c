#define _GNU_SOURCE
#include <unistd.h>

void drop(uid_t user, gid_t group)
{
    (void)setgid(group);
    (void)setuid(user);
}
