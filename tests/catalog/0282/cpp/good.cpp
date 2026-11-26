struct Base { virtual ~Base() = default; virtual void draw() const {} };
struct Derived : Base {
    ~Derived() override = default;
    void draw() const override {}
};
struct Last final : Derived { void draw() const final {} };
