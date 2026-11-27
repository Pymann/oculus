#include <vector>

struct Item {
    int id;
};

std::vector<::Item> items;   // '<' followed by '::', not a digraph
