struct Tag {};
struct Base {
    explicit Base(int value) : value(value) {}
    int value;
};

struct Derived : Tag, Base {
    explicit Derived(int extra) : Base(0), extra_(extra) {}
    Derived() : Derived(1) {}
private:
    int extra_;
};
