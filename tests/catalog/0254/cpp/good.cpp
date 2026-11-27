#include <string>

int whole(double value)
{
    std::string text = std::string("abc");
    return static_cast<int>(value) + static_cast<int>(text.size());
}
