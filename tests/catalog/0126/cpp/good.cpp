#include <algorithm>
#include <vector>

void cleanup(std::vector<int> &values)
{
    values.erase(std::remove(values.begin(), values.end(), 0), values.end());
    values.clear();
}
