namespace shapes {
int area(int side);
int area(int width, int height);
}

using shapes::area;

int use()
{
    return area(1) + area(1, 2);
}
