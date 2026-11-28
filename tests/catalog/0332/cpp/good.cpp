struct Base {
    virtual ~Base() = default;
    void put(int value) { (void)value; }
    virtual void show() const {}
};

struct Derived : Base {
    using Base::put;
    void put(long value) { (void)value; }
    void show() const override {}
};
