struct Base {
    Base() = default;
    explicit Base(int value) : value(value) {}
    int value = 0;
};

struct Derived : Base {
    Derived(int extra) : extra_(extra) {}
private:
    int extra_;
};
