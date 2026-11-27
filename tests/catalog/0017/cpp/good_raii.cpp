#include <mutex>

std::mutex table_lock;
int table_size;

int size()
{
    std::lock_guard<std::mutex> guard(table_lock);   // used for its constructor and destructor
    return table_size;
}
