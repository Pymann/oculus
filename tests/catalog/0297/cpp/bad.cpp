#include "primary.hpp"

template <>
struct Traits<int> { static constexpr bool special = true; };
