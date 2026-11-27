#include <memory>
#include <optional>

struct Item { int value; };

int reuse()
{
    std::optional<Item> item;
    item.emplace(Item{ 1 });
    item.reset();
    auto owned = std::make_unique<Item>(Item{ 2 });
    return owned->value;
}
