struct inner {
    int value;
    int scale;
};

struct outer {
    struct inner part;
    int count;
};

struct outer item = { .part.value = 1, .count = 5 };
