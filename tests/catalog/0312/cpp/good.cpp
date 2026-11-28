#include <stdexcept>

void close_handle(int handle);
void close_handle(int handle) { if (handle < 0) { throw std::runtime_error("bad"); } }

class File {
public:
    ~File()
    {
        try {
            close_handle(handle_);
        } catch (...) {
        }
    }
private:
    int handle_ = 0;
};
