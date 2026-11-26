#include <cstdint>

struct Flags {
    std::uint8_t bits;
    bool ready() const { return (bits & 0x01U) != 0U; }
};
