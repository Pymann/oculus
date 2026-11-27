#include <initializer_list>
#include <vector>

class Values {
public:
    Values(std::initializer_list<int> items) : items_(items) {}
    Values(int count, int value) : items_(static_cast<std::size_t>(count), value) {}
private:
    std::vector<int> items_;
};
