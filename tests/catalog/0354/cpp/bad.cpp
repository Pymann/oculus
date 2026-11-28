class Buffer {
public:
    Buffer() : data_(new int[8]) {}
    ~Buffer() { delete[] data_; }
private:
    int *data_;
};
