class Wrapper {
public:
    template <typename T>
    explicit Wrapper(const T &value) : size_(sizeof value) {}
    Wrapper(const Wrapper &other) = default;
private:
    unsigned long size_;
};
