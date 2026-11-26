#include <stdbool.h>
#include <stdint.h>

typedef unsigned int field_t;

struct flags {
    bool ready : 1;
    unsigned int mode : 3;
    field_t level : 4;
    signed int offset : 4;
};
