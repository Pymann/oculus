#define PICK(a, b) (a)

int value = PICK(1,
#ifdef FAST
    2
#else
    3
#endif
);
