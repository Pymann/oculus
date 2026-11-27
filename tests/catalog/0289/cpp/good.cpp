class Shape {
public:
    virtual ~Shape() = default;
    virtual double area() const = 0;
protected:
    Shape() = default;
    Shape(const Shape &) = default;
    Shape &operator=(const Shape &) = default;
};

class Circle final : public Shape {
public:
    double area() const override { return 3.0; }
};
