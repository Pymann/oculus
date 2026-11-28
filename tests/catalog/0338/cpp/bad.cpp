#include <functional>

class Counter {
public:
    std::function<int()> reader() const
    {
        return [=] { return value; };
    }
private:
    int value = 0;
};
