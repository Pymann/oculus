#include <mutex>

std::mutex table_lock;
int compute();

void touch()
{
    std::lock_guard<std::mutex> guard(table_lock);
    static_cast<void>(compute());
}
