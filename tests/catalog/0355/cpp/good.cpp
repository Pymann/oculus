#include <utility>

class Name {
public:
    Name() = default;
    Name(const Name &) = default;
    Name &operator=(const Name &) = default;
    Name(Name &&other) noexcept : size_(std::exchange(other.size_, 0)) {}
    Name &operator=(Name &&other) noexcept { size_ = std::exchange(other.size_, 0); return *this; }
    ~Name() = default;
private:
    int size_ = 0;
};
