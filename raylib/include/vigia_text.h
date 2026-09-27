#ifndef VIGIA_TEXT_H
#define VIGIA_TEXT_H

#include <stdbool.h>
#include <stddef.h>

#include "vigia_theme.h"


#define VIGIA_TEXT_LINE_CAP 64





bool vigia_text_fold(char *out, size_t capacity, const char *in);



int vigia_text_cells(const char *text, VigiaTextStyle style);




void vigia_text_upper(char *text);


size_t vigia_text_max_chars(int cells, VigiaTextStyle style);



void vigia_text_truncate(char *text, size_t max_chars);




bool vigia_text_wrap_next(const char **cursor, size_t max_chars,
                          char *out, size_t capacity);



size_t vigia_text_line_count(const char *text, size_t max_chars,
                             size_t max_lines);

#endif
