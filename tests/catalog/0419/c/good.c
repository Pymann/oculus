#include <stdlib.h>

extern int setenv(const char *name, const char *value, int overwrite);

int main(int argc, char *argv[], char *envp[])
{
    int has_entries = (envp[0] != NULL);
    (void)argc;
    (void)argv;
    (void)setenv("MODE", "test", 1);
    return has_entries ? 0 : (getenv("MODE") == NULL);
}
