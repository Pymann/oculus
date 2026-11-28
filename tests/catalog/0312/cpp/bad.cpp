#include <stdexcept>

class File {
public:
    ~File() noexcept(false)
    {
        if (open_) {
            throw std::runtime_error("still open");
        }
    }
private:
    bool open_ = false;
};
