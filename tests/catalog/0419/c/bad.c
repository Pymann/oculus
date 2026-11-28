#include <stdlib.h>

extern int setenv(const char *name, const char *value, int overwrite);

int main(int argc, char *argv[], char *envp[])
{
    (void)argc;
    (void)argv;
    (void)setenv("MODE", "test", 1);
    return (envp[0] != NULL) ? 0 : 1;
}
