#include <utility>

class Buffer {
public:
    Buffer() = default;
    Buffer(Buffer &&other) noexcept : size_(std::exchange(other.size_, 0)) {}
    Buffer &operator=(Buffer &&other) = default;
    ~Buffer() {}
    friend void swap(Buffer &left, Buffer &right) noexcept { std::swap(left.size_, right.size_); }
private:
    int size_ = 0;
};
