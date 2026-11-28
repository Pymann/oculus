#include <vector>

bool append(std::vector<int> &items, int value) noexcept
{
    try {
        items.push_back(value);
        return true;
    } catch (...) {
        return false;
    }
}

std::size_t count(const std::vector<int> &items) noexcept
{
    return items.size();
}
