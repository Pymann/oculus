#include <cstddef>

class Account {
public:
    double balance() const { return balance_; }
    void deposit(double amount) { balance_ += amount; }
    Account &operator=(const Account &other) = default;
private:
    double balance_ = 0.0;
};

class Row {
public:
    int &operator[](std::size_t index) { return cells_[index]; }
private:
    int cells_[4] = {};
};
