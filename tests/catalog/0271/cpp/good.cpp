constexpr int default_factor = 2;

struct Base {
    virtual ~Base() = default;
    virtual int scale(int factor = default_factor) const { return factor; }
};

struct Same : Base {
    int scale(int factor = 2) const override { return factor * 10; }
};

struct Without : Base {
    int scale(int factor) const override { return factor * 20; }
};
