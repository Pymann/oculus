struct Base { virtual ~Base() = default; virtual int value() const { return 0; } };
struct Middle : Base { int value() const override = 0; };
