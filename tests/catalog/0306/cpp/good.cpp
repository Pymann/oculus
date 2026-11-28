#include <string>
#include <string_view>

constexpr std::string_view greeting = "hello, world";

const std::string &name()
{
    static const std::string value("allocated at first call");
    return value;
}
