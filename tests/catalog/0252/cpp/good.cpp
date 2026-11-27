struct Base { virtual ~Base() = default; };
struct Derived : virtual Base { int value = 0; };

int read(Base *base)
{
    Derived *derived = dynamic_cast<Derived *>(base);
    return (derived != nullptr) ? derived->value : 0;
}
