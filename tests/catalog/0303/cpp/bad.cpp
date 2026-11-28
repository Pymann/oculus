#include <string>

struct Error {
    explicit Error(const std::string &text) : text(text) {}
    std::string text;
};

void fail(const std::string &where)
{
    throw Error("failure in " + where);
}
