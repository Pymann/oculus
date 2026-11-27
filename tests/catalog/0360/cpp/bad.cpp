#include <utility>

class Buffer {
public:
    Buffer() = default;
    Buffer(Buffer &&other) : size_(std::exchange(other.size_, 0)) {}
private:
    int size_ = 0;
};
