struct Shape { virtual ~Shape() = default; };
struct Circle : Shape { double radius = 1.0; };

double radius_of(Shape &shape)
{
    return static_cast<Circle &>(shape).radius;
}
