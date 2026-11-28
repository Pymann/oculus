#include <stdexcept>

[[noreturn]] void stop()
{
    throw std::runtime_error("stop");
}
