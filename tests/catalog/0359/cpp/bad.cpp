class Money {
public:
    explicit Money(long cents) : cents_(cents) {}
    Money operator+(const Money &other) const { return Money(cents_ + other.cents_); }
private:
    long cents_;
};
