#include <stdio.h>

struct box { int left; int top; int right; int bottom; };

int area(const struct box *box);
int log_line(const char *format, int first, int second, ...);
int none(void);
