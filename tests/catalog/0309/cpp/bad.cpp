#include <stdexcept>

int run()
{
    try {
        throw std::out_of_range("index");
    } catch (std::exception error) {
        return 1;
    }
}
