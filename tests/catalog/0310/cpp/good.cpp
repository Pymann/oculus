#include <new>
#include <stdexcept>

int run()
{
    try {
        throw std::out_of_range("index");
    } catch (const std::out_of_range &) {
        return 2;
    } catch (const std::bad_alloc &) {
        return 3;
    } catch (const std::exception &) {
        return 1;
    }
}
