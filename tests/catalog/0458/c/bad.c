#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <sys/stat.h>

int open_regular(const char *path)
{
    struct stat info;
    if ((lstat(path, &info) != 0) || !S_ISREG(info.st_mode)) {
        return -1;
    }
    return open(path, O_RDONLY);
}
