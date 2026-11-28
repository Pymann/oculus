#include <vector>

void append(std::vector<int> &items, int value) noexcept
{
    items.push_back(value);
}
