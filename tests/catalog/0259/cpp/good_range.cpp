#include <vector>

int sum_positive(const std::vector<int> &items)
{
    int sum = 0;
    for (int value : items) {
        if (value < 0) {
            continue;
        }
        sum += value;
    }
    return sum;
}
