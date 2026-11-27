#include <string>
#include <utility>

std::string keep(std::string name)
{
    return std::move(name);
}

struct Holder {
    std::string value;
    void set(std::string text) { value = std::move(text); }
};
