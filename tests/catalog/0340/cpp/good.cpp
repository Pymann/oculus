#include <cstddef>

const unsigned char *bytes(const int &value)
{
    return reinterpret_cast<const unsigned char *>(&value);   // allowed: byte view
}
