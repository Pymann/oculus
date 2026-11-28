#include <algorithm>
#include <functional>
#include <vector>

std::function<int()> make(int base)
{
    int offset = 3;
    return [base, offset] { return base + offset; };
}

int count_above(const std::vector<int> &items, int limit)
{
    return static_cast<int>(std::count_if(items.begin(), items.end(),
                                          [&](int item) { return item > limit; }));
}
