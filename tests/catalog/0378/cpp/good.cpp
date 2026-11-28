#include <string>
#include <utility>
#include <vector>

template <typename T>
void add(std::vector<std::string> &items, T &&item)
{
    items.emplace_back(std::forward<T>(item));
}

template <typename T>
std::size_t size_of(T &&item)
{
    return item.size();   /* only read, not passed on */
}

void use(std::vector<std::string> &items)
{
    add(items, std::string("x"));
    auto forwarder = [&items](auto &&value) { items.emplace_back(std::forward<decltype(value)>(value)); };
    forwarder("y");
    (void)size_of(items);
}
