#include <time.h>

const char *now_text(void)
{
    time_t now = time(NULL);
    const struct tm *moment = localtime(&now);
    return (moment != NULL) ? asctime(moment) : "";
}

size_t show(char *buffer, size_t size, int year)
{
    struct tm moment = { 0 };
    moment.tm_year = year - 1900;
    return strftime(buffer, size, "%Y-%m-%d", &moment);
}
