#include <string>

class Timeout {
public:
    explicit Timeout(int milliseconds) : ms_(milliseconds) {}
    Timeout(const Timeout &other) = default;
private:
    int ms_;
};

class Name {
public:
    Name(std::string text) : text_(std::move(text)) {}   /* class type argument */
private:
    std::string text_;
};
