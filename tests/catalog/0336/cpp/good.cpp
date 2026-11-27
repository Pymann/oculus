class Builder {
public:
    Builder &width(int value) & { width_ = value; return *this; }
    Builder width(int value) && { width_ = value; return *this; }
    Builder &operator=(const Builder &other) = default;
private:
    int width_ = 0;
};
