#include <vector>

using flags_t = bool;

std::size_t count_set(const std::vector<flags_t> &flags)
{
    std::size_t count = 0U;
    for (bool flag : flags) {
        count += flag ? 1U : 0U;
    }
    return count;
}
