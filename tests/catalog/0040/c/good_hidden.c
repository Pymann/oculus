#include <stdint.h>

typedef unsigned int field_t;
typedef field_t other_t;       /* a typedef of a typedef */
#define FLAG_TYPE unsigned int

struct settings {
    int32_t delta : 8;         /* stdint spells it signed int */
    other_t mask : 4;
    FLAG_TYPE enabled : 1;
    signed sign_only : 2;      /* "signed" alone is signed int */
    const unsigned int fixed : 3;
    _Bool done : 1;
    int whole;                 /* not a bit-field */
};
