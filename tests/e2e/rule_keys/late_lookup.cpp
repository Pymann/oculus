template <typename T>
int describe(const T &value) { return code_of(value); }

namespace model {
struct Item {};
int code_of(const Item &) { return 7; }
}

int use() { return describe(model::Item{}); }
