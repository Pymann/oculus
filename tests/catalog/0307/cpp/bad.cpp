#include <vector>

int main()
{
    std::vector<int> values(1000U);
    return static_cast<int>(values.size() % 2U);
}
