void flush() noexcept;

void flush() noexcept {}

struct Base { virtual ~Base() = default; virtual void run() {} };
struct Derived : Base { void run() noexcept override {} };
