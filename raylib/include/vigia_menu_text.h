#ifndef VIGIA_MENU_TEXT_H
#define VIGIA_MENU_TEXT_H

#include <stdbool.h>
#include <stddef.h>

#define VIGIA_MENU_LINE_CAP 256U
#define VIGIA_MENU_DOT 0x00B7
#define VIGIA_MENU_DASH 0x2014

typedef struct {
    int glyphs[VIGIA_MENU_LINE_CAP];
    size_t count;
    float width;
    bool has_separator;
} VigiaMenuLine;



bool vigia_menu_line_literal(VigiaMenuLine *line, const char *text);
bool vigia_menu_line_pair(VigiaMenuLine *line, const char *fields, int separator);
void vigia_menu_line_elide(VigiaMenuLine *line, float max_width);
float vigia_menu_glyph_advance(int codepoint);
float vigia_menu_line_height(const VigiaMenuLine *line);
float vigia_menu_line_baseline(const VigiaMenuLine *line);

#endif
