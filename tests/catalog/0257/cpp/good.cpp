#include <cmath>

bool same(double left, double right)
{
    return std::fabs(left - right) < 1e-9;
}
