#ifndef VIGIA_CLOCK_H
#define VIGIA_CLOCK_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

bool vigia_clock_format(time_t timestamp, char *out, size_t capacity);

#endif
