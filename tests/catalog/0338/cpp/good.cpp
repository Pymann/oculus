#include <algorithm>
#include <functional>
#include <vector>

class Counter {
public:
    std::function<int()> reader() const
    {
        int copy = value;
        return [copy] { return copy; };
    }
    int count_above(const std::vector<int> &items) const
    {
        return static_cast<int>(std::count_if(items.begin(), items.end(),
                                              [&](int item) { return item > value; }));
    }
private:
    int value = 0;
};
