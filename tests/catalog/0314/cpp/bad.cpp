#include <thread>

void work() {}

void start()
{
    std::thread worker(work);
}
