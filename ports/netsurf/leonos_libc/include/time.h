#ifndef LEONOS_LIBC_TIME_H
#define LEONOS_LIBC_TIME_H

#include <stddef.h>

typedef long time_t;

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
    long tm_gmtoff;
};

time_t time(time_t *timer);
double difftime(time_t time1, time_t time0);
time_t mktime(struct tm *tm);
struct tm *localtime(const time_t *timer);
struct tm *localtime_r(const time_t *timer, struct tm *result);
struct tm *gmtime(const time_t *timer);
size_t strftime(char *s, size_t max, const char *format, const struct tm *tm);

#endif
