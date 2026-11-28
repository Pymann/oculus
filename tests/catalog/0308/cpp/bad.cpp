#include <string>

class Config {
public:
    explicit Config(const char *path)
    try : path_(path) {
    } catch (...) {
        path_.clear();
    }
private:
    std::string path_;
};
