#ifndef VIGIA_DRAW_H
#define VIGIA_DRAW_H

#include <stdbool.h>

#include "raylib.h"
#include "vigia_layout.h"
#include "vigia_theme.h"
#include "vigia_menu_text.h"

typedef struct {
    Font font;
    bool owns_font;
} VigiaDraw;




bool vigia_draw_init(VigiaDraw *draw);
void vigia_draw_shutdown(VigiaDraw *draw);

Color vigia_color(unsigned int rgba, float alpha);

Color vigia_menu_color(unsigned int rgba, float alpha);





void vigia_draw_menu_line(const VigiaDraw *draw, const VigiaMenuLine *line,
                          float x, float y, float horizontal_offset,
                          unsigned int rgba, float alpha);


void vigia_draw_surface(VigiaRect rect, float alpha);


void vigia_draw_reverse(const VigiaDraw *draw, VigiaRect rect,
                        const char *text, float alpha);


void vigia_draw_text(const VigiaDraw *draw, VigiaRect content, int col,
                     int row, const char *text, VigiaTextStyle style,
                     float alpha);
void vigia_draw_text_right(const VigiaDraw *draw, VigiaRect content,
                           int cols_from_right, int row, const char *text,
                           VigiaTextStyle style, float alpha);
void vigia_draw_text_centre(const VigiaDraw *draw, VigiaRect content, int row,
                            const char *text, VigiaTextStyle style,
                            float alpha);


void vigia_draw_text_tinted(const VigiaDraw *draw, VigiaRect content, int col,
                            int row, const char *text, VigiaTextStyle style,
                            unsigned int rgba, float alpha);

#endif
