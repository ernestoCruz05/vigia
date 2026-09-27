#include "vigia_menu_text.h"
#include "vigia_text.h"

#include <math.h>
#include <string.h>

static bool is_separator(int codepoint)
{
    return codepoint == VIGIA_MENU_DOT || codepoint == VIGIA_MENU_DASH;
}

float vigia_menu_glyph_advance(int codepoint)
{
    return is_separator(codepoint) ? 9.59375F : 8.0F;
}

float vigia_menu_line_height(const VigiaMenuLine *line)
{
    return line != NULL && line->has_separator ? 19.0F : 16.0F;
}

float vigia_menu_line_baseline(const VigiaMenuLine *line)
{
    return line != NULL && line->has_separator ? 13.3125F : 13.0F;
}

static bool append_glyph(VigiaMenuLine *line, int codepoint)
{
    if (line->count >= VIGIA_MENU_LINE_CAP) {
        return false;
    }
    line->glyphs[line->count++] = codepoint;
    line->width += vigia_menu_glyph_advance(codepoint);
    line->has_separator = line->has_separator || is_separator(codepoint);
    return true;
}

static bool append_text(VigiaMenuLine *line, const char *text)
{
    char folded[VIGIA_MENU_LINE_CAP + 1U] = {0};
    const bool fits = vigia_text_fold(folded, sizeof folded, text);
    for (size_t i = 0U; folded[i] != '\0'; i++) {
        if (!append_glyph(line, (unsigned char)folded[i])) {
            return false;
        }
    }
    return fits;
}

bool vigia_menu_line_literal(VigiaMenuLine *line, const char *text)
{
    if (line == NULL) {
        return false;
    }
    *line = (VigiaMenuLine){0};
    return append_text(line, text);
}

bool vigia_menu_line_pair(VigiaMenuLine *line, const char *fields, int separator)
{
    if (line == NULL) {
        return false;
    }
    *line = (VigiaMenuLine){0};
    if (!is_separator(separator)) {
        return false;
    }
    const char *const split = fields != NULL ? strchr(fields, '\t') : NULL;
    if (split == NULL) {
        return append_text(line, fields);
    }
    const size_t length = (size_t)(split - fields);
    const size_t take = length < VIGIA_MENU_LINE_CAP ? length : VIGIA_MENU_LINE_CAP;
    char left[VIGIA_MENU_LINE_CAP + 1U] = {0};


    memcpy(left, fields, take);
    const bool left_fits = append_text(line, left) && take == length;
    if (line->count > 0U && split[1] != '\0') {
        if (!append_glyph(line, ' ') || !append_glyph(line, separator)
            || !append_glyph(line, ' ')) {
            return false;
        }
    }
    const bool right_fits = append_text(line, split + 1);
    return left_fits && right_fits;
}

static void measure(VigiaMenuLine *line)
{
    line->width = 0.0F;
    line->has_separator = false;
    for (size_t i = 0U; i < line->count; i++) {
        line->width += vigia_menu_glyph_advance(line->glyphs[i]);
        line->has_separator = line->has_separator || is_separator(line->glyphs[i]);
    }
}

void vigia_menu_line_elide(VigiaMenuLine *line, float max_width)
{
    if (line == NULL) {
        return;
    }
    if (!isfinite(max_width) || max_width <= 0.0F) {
        *line = (VigiaMenuLine){0};
        return;
    }
    if (line->count > VIGIA_MENU_LINE_CAP) {
        line->count = VIGIA_MENU_LINE_CAP;
    }
    measure(line);
    if (line->width <= max_width) {
        return;
    }
    const size_t dots = max_width >= 24.0F ? 3U : (size_t)(max_width / 8.0F);
    const float suffix_width = (float)dots * 8.0F;
    while (line->count > 0U && (line->width + suffix_width > max_width
                               || line->count + dots > VIGIA_MENU_LINE_CAP)) {
        line->width -= vigia_menu_glyph_advance(line->glyphs[--line->count]);
    }
    for (size_t i = 0U; i < dots; i++) {
        (void)append_glyph(line, '.');
    }
    measure(line);
}
