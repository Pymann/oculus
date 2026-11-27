#include <cerrno>
#include <cstdlib>

long parse(const char *text)
{
    errno = 0;
    return std::strtol(text, nullptr, 10);
}
