#include <cstdint>

using field_t = unsigned int;

template <typename T>
struct Holder {
    T value : 4;
};

struct Settings {
    std::int32_t delta : 8;
    field_t mask : 4;
    signed int offset : 4;
    Holder<unsigned int> holder;   /* the instantiation is unsigned int */
};
