#include <compare>

class Money {
public:
    explicit Money(long cents) : cents_(cents) {}
    Money &operator+=(const Money &other) { cents_ += other.cents_; return *this; }
    bool operator==(const Money &) const = default;
    friend Money operator+(Money left, const Money &right) { left += right; return left; }
private:
    long cents_;
};
