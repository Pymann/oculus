#include <stdexcept>

void fail()
{
    throw new std::runtime_error("failure");
}
