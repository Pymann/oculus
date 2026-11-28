#include <concepts>

namespace shapes {

struct Circle { double radius; };

template <typename T>
    requires std::same_as<T, Circle>
bool same(const T &left, const T &right) { return left.radius == right.radius; }

}

namespace algorithms {

template <typename T>
T twice(const T &value) { return value + value; }

}
