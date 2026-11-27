#include <new>

struct Item { int value; };

void reuse(void *storage)
{
    Item *item = new (storage) Item{ 1 };
    item->~Item();
}
