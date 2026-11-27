#define PICK(a, b) (a)

#ifdef FAST
#define SECOND 2
#else
#define SECOND 3
#endif

int value = PICK(1, SECOND);
