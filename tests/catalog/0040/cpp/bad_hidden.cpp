#include <cstdint>

using plain_t = int;

template <typename T>
struct Holder {
    T value : 4;
};

struct Packed {
    plain_t level : 4;
    short half : 3;
    std::uint8_t small : 3;
};

struct Uses {
    Holder<int> holder;     /* point of instantiation: its bit-field is a plain int */
};
