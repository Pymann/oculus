class Name {
public:
    Name() = default;
    Name(const Name &) = default;
    Name &operator=(const Name &) = default;
    Name(Name &&other) { (void)other; }
    Name &operator=(Name &&) noexcept = default;
    ~Name() = default;
};
