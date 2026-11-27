#include <cctype>

bool is_digit(char value)
{
    return std::isdigit(static_cast<unsigned char>(value)) != 0;
}
