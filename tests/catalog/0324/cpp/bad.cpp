#include <cstddef>

struct Header {
    int tag;
    int length;
};

std::size_t length_offset = offsetof(Header, length);
