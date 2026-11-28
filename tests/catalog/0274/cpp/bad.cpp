class Account {
public:
    double &balance() { return balance_; }
private:
    double balance_ = 0.0;
};
