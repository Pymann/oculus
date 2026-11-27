struct Base { virtual ~Base() = default; };
struct Derived : virtual Base { int value = 0; };

int read(Base *base)
{
    Derived *derived = reinterpret_cast<Derived *>(base);
    return derived->value;
}
