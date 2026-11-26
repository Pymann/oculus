#include <stdexcept>

void run()
{
    try {
        throw std::runtime_error("x");
    } catch (const std::exception &) {
        if (true) {
            throw;
        }
    }
}
