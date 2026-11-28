int larger(int left, int right)
{
    return (left > right) ? left : right;
}

int &first(int &left, int &right)
{
    static_cast<void>(right);
    return left;   // non-const lvalue reference: no temporary can bind
}

struct Counter {
    int value = 0;
    Counter &add(int step) { value += step; return *this; }
};
