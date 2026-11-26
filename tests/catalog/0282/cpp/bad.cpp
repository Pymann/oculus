struct Base { virtual ~Base() = default; virtual void draw() const {} };
struct Derived : Base { void draw() const {} };
