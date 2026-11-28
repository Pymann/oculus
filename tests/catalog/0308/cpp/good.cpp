#include <string>

class Config {
public:
    explicit Config(const char *path)
    try : path_(path) {
    } catch (...) {
        ++failures;
    }
    static int failures;
private:
    std::string path_;
};

int Config::failures = 0;
