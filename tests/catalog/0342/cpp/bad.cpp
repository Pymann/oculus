// expect-compile-error
#include <cstdio>
#include <string>

void show(const std::string &text)
{
    std::printf("%s\n", text);
}
