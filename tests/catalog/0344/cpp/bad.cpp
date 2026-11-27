#include <mutex>

std::mutex table_lock;

void touch()
{
    std::unique_lock<std::mutex>(table_lock);
}
