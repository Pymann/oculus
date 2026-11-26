struct Base { virtual ~Base() = default; virtual int value() const = 0; };
struct Derived : Base { int value() const override { return 1; } };
