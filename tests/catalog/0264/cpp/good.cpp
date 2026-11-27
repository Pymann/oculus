#include "good.hpp"

namespace {
int hidden = 0;   // unnamed namespace in a source file is fine
}

namespace detail {
int counter() { return hidden; }
}
