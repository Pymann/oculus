namespace model {
struct Item {};
int code_of(const Item &) { return 7; }
}

template <typename T>
int describe(const T &value) { return model::code_of(value); }

int use() { return describe(model::Item{}); }
