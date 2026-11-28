#include <memory>
#include <vector>

struct Node { int value; };

int read_value()
{
    auto node = std::make_unique<Node>(Node{ 7 });
    std::vector<Node> nodes(3U);
    return node->value + static_cast<int>(nodes.size());
}
