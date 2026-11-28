#include <thread>

void work() {}

void start()
{
    std::thread worker(work);
    worker.join();
    std::jthread joined(work);   /* joins in its destructor */
}
