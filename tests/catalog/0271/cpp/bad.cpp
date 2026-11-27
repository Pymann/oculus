struct Base {
    virtual ~Base() = default;
    virtual int scale(int factor = 2) const { return factor; }
};

struct Derived : Base {
    int scale(int factor = 3) const override { return factor * 10; }
};
