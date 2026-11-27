#include <memory>

struct Item { int value; };

std::shared_ptr<Item> make_item()
{
    return std::make_shared<Item>(Item{ 3 });
}

std::shared_ptr<Item> share(std::unique_ptr<Item> owned)
{
    std::unique_ptr<Item> empty(nullptr);
    return std::shared_ptr<Item>(std::move(owned));
}
