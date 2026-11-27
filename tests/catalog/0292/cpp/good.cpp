class Value {
public:
    template <typename T>
    Value &operator=(const T &other) { size_ = sizeof other; return *this; }
    Value &operator=(const Value &other) = default;
private:
    unsigned long size_ = 0U;
};
