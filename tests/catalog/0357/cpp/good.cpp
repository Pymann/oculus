#include <initializer_list>
#include <vector>

class Values {
public:
    Values() = default;
    Values(std::initializer_list<int> items) : items_(items) {}
    Values(const Values &) = default;
    static Values repeated(int count, int value)
    {
        Values result;
        result.items_.assign(static_cast<std::size_t>(count), value);
        return result;
    }
private:
    std::vector<int> items_;
};
