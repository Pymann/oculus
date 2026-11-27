#include <string>
#include <utility>

std::string keep(const std::string &name)
{
    return std::move(name);
}
