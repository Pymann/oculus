#include <stdexcept>

int run()
{
    try {
        throw std::out_of_range("index");
    } catch (const std::exception &) {
        return 1;
    } catch (const std::out_of_range &) {
        return 2;
    }
}
