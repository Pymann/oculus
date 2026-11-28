#include <vector>

int main()
{
    try {
        std::vector<int> values(1000U);
        return static_cast<int>(values.size() % 2U);
    } catch (...) {
        return 1;
    }
}
