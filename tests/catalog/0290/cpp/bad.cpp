namespace shapes {

struct Circle { double radius; };

template <typename T>
bool operator==(const T &left, const T &right) { return &left == &right; }

}
