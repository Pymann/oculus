class Buffer {
public:
    const int *data() const { return items_; }
    int *data() { return items_; }
    int first() const { return (items_ != nullptr) ? items_[0] : 0; }
private:
    int *items_ = nullptr;
};
