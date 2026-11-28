#include <time.h>

const char *show(int year)
{
    struct tm moment = { 0 };
    moment.tm_year = year - 1900;
    return asctime(&moment);
}
