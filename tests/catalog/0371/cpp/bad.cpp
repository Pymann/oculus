#include <memory>

struct Item { int value; };

std::shared_ptr<Item> make_item()
{
    return std::shared_ptr<Item>(new Item{ 3 });
}
