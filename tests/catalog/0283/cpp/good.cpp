struct Base { virtual ~Base() = default; virtual int value() const = 0; };
struct Middle : Base { int value() const override = 0; };
struct Leaf : Middle { int value() const override { return 1; } };
