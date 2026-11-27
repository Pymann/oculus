#include <utility>

int sum(std::pair<int, int> values)
{
    auto [left, right] = values;   // one declaration, one binding group
    return left + right;
}
