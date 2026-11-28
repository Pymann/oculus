class Buffer {
public:
    int *data() const { return items_; }
private:
    int *items_ = nullptr;
};
