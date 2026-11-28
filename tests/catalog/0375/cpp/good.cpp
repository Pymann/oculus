#include <bitset>
#include <cstdint>
#include <vector>

struct Flag { bool value; };

std::size_t count_set(const std::vector<std::uint8_t> &flags)
{
    std::size_t count = 0U;
    for (std::uint8_t flag : flags) {
        count += (flag != 0U) ? 1U : 0U;
    }
    return count;
}

std::size_t count_set(const std::bitset<32> &flags) { return flags.count(); }

std::vector<Flag> make_flags() { return std::vector<Flag>(4U); }
