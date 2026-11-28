namespace shapes {
int area(int side);
}

using shapes::area;

namespace shapes {
int area(int width, int height);
}

int use()
{
    return area(1);
}
