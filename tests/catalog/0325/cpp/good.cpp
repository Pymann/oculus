#include <charconv>
#include <system_error>
#include <cstring>

bool parse(const char *text, long &value)
{
    auto result = std::from_chars(text, text + std::strlen(text), value);
    return result.ec == std::errc();
}
