#include <vector>

class Buffer {
public:
    Buffer() : data_(8U) {}
private:
    std::vector<int> data_;
};

class Handle {
public:
    Handle() = default;
    ~Handle() = default;
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    Handle(Handle &&) noexcept = default;
    Handle &operator=(Handle &&) noexcept = default;
};
