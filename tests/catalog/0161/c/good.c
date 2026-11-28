struct reader {
    int (*read)(void);   /* member named like a POSIX function: different scope */
};

static int counter_value;
