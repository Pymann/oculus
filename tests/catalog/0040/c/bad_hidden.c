#include <stdint.h>

typedef int plain_t;
#define BITS int

struct packed {
    plain_t level : 4;      /* the typedef hides a plain int */
    BITS kind : 2;          /* reported at the macro call site */
    uint8_t small : 3;      /* unsigned char, not unsigned int */
    long wide : 5;
    char letter : 7;
};
