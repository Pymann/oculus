#define SWAP(a, b) \
    do { int keep_ = (a); (a) = (b); (b) = keep_; } while (0)
