#include "primary.hpp"
#include <cstddef>
#include <functional>

struct Key { int id; };

template <>
struct Traits<Key> { static constexpr bool special = true; };

template <>
struct std::hash<Key> {
    std::size_t operator()(const Key &key) const { return static_cast<std::size_t>(key.id); }
};
