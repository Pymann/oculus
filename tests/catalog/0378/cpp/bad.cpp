#include <string>
#include <utility>
#include <vector>

template <typename T>
void add(std::vector<std::string> &items, T &&item)
{
    items.emplace_back(item);
}

void use(std::vector<std::string> &items) { add(items, std::string("x")); }
