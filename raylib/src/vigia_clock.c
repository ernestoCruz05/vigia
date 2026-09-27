#include "vigia_clock.h"

#include <stdio.h>

bool vigia_clock_format(time_t timestamp, char *out, size_t capacity)
{
    if (out == NULL || capacity == 0U) {
        return false;
    }
    out[0] = '\0';
    struct tm local = {0};
    if (localtime_r(&timestamp, &local) == NULL || local.tm_wday < 0 || local.tm_wday > 6) {
        return false;
    }
    const char *const weekdays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    const int written = snprintf(out, capacity, "%s %02d\t%02d:%02d",
        weekdays[local.tm_wday], local.tm_mday, local.tm_hour, local.tm_min);
    if (written < 0 || (size_t)written >= capacity) {
        out[0] = '\0';
        return false;
    }
    return true;
}
