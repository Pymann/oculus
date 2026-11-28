struct Base { virtual ~Base() = default; virtual int id() const = 0; };
struct Left : virtual Base { };
struct Right : virtual Base { };
struct Final : Left, Right { int id() const override { return 3; } };
