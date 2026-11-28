#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

int open_regular(const char *path)
{
    struct stat info;
    int handle = open(path, O_RDONLY | O_NOFOLLOW);
    if (handle == -1) {
        return -1;
    }
    if ((fstat(handle, &info) != 0) || !S_ISREG(info.st_mode)) {
        (void)close(handle);
        return -1;
    }
    return handle;
}
