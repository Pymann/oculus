class Wrapper {
public:
    template <typename T>
    explicit Wrapper(const T &value) : size_(sizeof value) {}
private:
    unsigned long size_;
};
