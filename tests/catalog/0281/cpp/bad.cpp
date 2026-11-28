struct Base { virtual ~Base() = default; virtual int id() const { return 0; } };
struct Middle : Base { int id() const override { return 1; } };
struct Leaf : Middle { int id() const override { return 2; } };
