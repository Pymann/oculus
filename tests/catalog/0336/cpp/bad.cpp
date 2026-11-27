class Builder {
public:
    Builder &width(int value) { width_ = value; return *this; }
private:
    int width_ = 0;
};
