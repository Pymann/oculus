struct Shape {
    virtual ~Shape() = default;
    virtual double size() const = 0;
};
struct Circle : Shape {
    double radius = 1.0;
    double size() const override { return radius; }
};

double size_of(const Shape &shape) { return shape.size(); }

const Shape &as_shape(const Circle &circle)
{
    return static_cast<const Shape &>(circle);   /* upcast */
}
