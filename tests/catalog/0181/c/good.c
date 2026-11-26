struct parts {
    unsigned int low : 8;
    unsigned int high : 8;
};

union word {
    unsigned int raw;
    struct parts split;   /* bit-fields inside a nested structure */
};
