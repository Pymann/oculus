struct Point {
    int x;
    int y;
};

void copy(Point &target, const Point &source)
{
    target = source;
}
