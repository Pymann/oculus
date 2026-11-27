#include <array>
#include <cstring>

int sum(const std::array<int, 4> &values);
void log_text(const char *text);

int total()
{
    std::array<int, 4> values = { 1, 2, 3, 4 };
    char name[8] = "abc";
    log_text("literal");
    return sum(values) + static_cast<int>(std::strlen(name));
}
