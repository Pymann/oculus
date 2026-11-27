#include <stdexcept>

int run()
{
    try {
        throw std::out_of_range("index");
    } catch (const std::exception &error) {
        return (error.what() != nullptr) ? 1 : 2;
    } catch (int code) {
        return code;
    }
}
