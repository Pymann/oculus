struct Base {
    void put(int value) { (void)value; }
    void put(double value) { (void)value; }
};

struct Derived : Base {
    void put(long value) { (void)value; }
};
