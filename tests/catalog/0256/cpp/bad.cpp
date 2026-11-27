class Handle {
public:
    int *operator&() { return &value_; }
private:
    int value_ = 0;
};
