#include "vigia_draw.h"
#include "rlgl.h"
#include "vigia_text.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

Color vigia_menu_color(unsigned int rgba, float alpha)
{
    if (!isfinite(alpha)) {
        return (Color){0};
    }
    const float opacity = fminf(1.0F, fmaxf(0.0F, alpha)) * (float)(rgba & 0xFFU) / 255.0F;
    return (Color){
        .r = (unsigned char)roundf((float)((rgba >> 24U) & 0xFFU) * opacity),
        .g = (unsigned char)roundf((float)((rgba >> 16U) & 0xFFU) * opacity),
        .b = (unsigned char)roundf((float)((rgba >> 8U) & 0xFFU) * opacity),
        .a = (unsigned char)roundf(255.0F * opacity),
    };
}





static const unsigned int dot_coverage[16] = {
    0x00000DU, 0x223841U, 0x42423FU, 0x2B1502U,
    0x00001BU, 0x60B4EFU, 0xFFF7C6U, 0x732602U,
    0x00001BU, 0x5DB0EAU, 0xFCF3C3U, 0x702602U,
    0x000000U, 0x020508U, 0x090908U, 0x050200U,
};
static const unsigned int dash_coverage[24] = {
    0x000515U, 0x28373BU, 0x3C3C3CU, 0x3C3C3CU, 0x3C3C3CU, 0x3C3C3CU,
    0x3C3C3CU, 0x3C3C3CU, 0x3C3C3CU, 0x3C3C3CU, 0x3B3728U, 0x150500U,
    0x000837U, 0x7CBBD3U, 0xD4D4D4U, 0xD4D4D4U, 0xD4D4D4U, 0xD4D4D4U,
    0xD4D4D4U, 0xD4D4D4U, 0xD4D4D4U, 0xD4D4D4U, 0xD4BA7CU, 0x350800U,
};
static const unsigned int dash_half_coverage[22] = {
    0x0D2135U, 0x3C3C3CU, 0x3C3C3CU, 0x3C3C3CU, 0x3C3C3CU, 0x3C3C3CU,
    0x3C3C3CU, 0x3C3C3CU, 0x3C3C3CU, 0x3C3C3CU, 0x34200CU,
    0x1B57A0U, 0xCDD8D5U, 0xD4D4D4U, 0xD4D4D4U, 0xD4D4D4U, 0xD4D4D4U,
    0xD4D4D4U, 0xD4D4D4U, 0xD4D4D4U, 0xD6D8CDU, 0x9F561AU,
};

static void begin_menu_text_blend(void)
{
    rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA,
        RL_ONE, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD, RL_FUNC_ADD);
    BeginBlendMode(BLEND_CUSTOM_SEPARATE);
}


static void draw_separator(int codepoint, float x, float baseline,
                            unsigned int rgba, float alpha)
{
    if (!isfinite(x) || !isfinite(baseline)) {
        return;
    }
    const bool dot = codepoint == VIGIA_MENU_DOT;
    const bool half_phase = x - floorf(x) >= 0.5F;
    const unsigned int *const coverage = dot ? dot_coverage
        : (half_phase ? dash_half_coverage : dash_coverage);
    const size_t width = dot ? 4U : (half_phase ? 11U : 12U);
    const size_t height = dot ? 4U : 2U;
    const float left = roundf(x + (dot ? 3.0F : -1.0F));
    const float top = roundf(baseline - (dot ? 6.0F : 5.0F));
    const float opacity = fminf(1.0F, fmaxf(0.0F, alpha)) * (float)(rgba & 0xFFU) / 255.0F;
    const Color tint = vigia_color(rgba, 1.0F);




    for (size_t pass = 0U; pass < 2U; pass++) {
        rlSetBlendFactorsSeparate(pass == 0U ? RL_ZERO : RL_ONE,
            pass == 0U ? RL_ONE_MINUS_SRC_COLOR : RL_ONE,
            RL_ZERO, RL_ONE, RL_FUNC_ADD, RL_FUNC_ADD);
        BeginBlendMode(BLEND_CUSTOM_SEPARATE);
        for (size_t row = 0U; row < height; row++) {
            for (size_t col = 0U; col < width; col++) {
                Color color = vigia_menu_color((coverage[row * width + col] << 8U) | 0xFFU,
                                               opacity);
                if (pass != 0U) {
                    color.r = (unsigned char)(((unsigned int)tint.r * color.r + 127U) / 255U);
                    color.g = (unsigned char)(((unsigned int)tint.g * color.g + 127U) / 255U);
                    color.b = (unsigned char)(((unsigned int)tint.b * color.b + 127U) / 255U);
                }
                DrawRectangleRec((Rectangle){ .x = left + (float)col, .y = top + (float)row,
                    .width = 1.0F, .height = 1.0F }, color);
            }
        }
    }
    begin_menu_text_blend();
}


void vigia_draw_menu_line(const VigiaDraw *draw, const VigiaMenuLine *line,
                          float x, float y, float horizontal_offset,
                          unsigned int rgba, float alpha)
{
    if (draw == NULL || line == NULL || line->count > VIGIA_MENU_LINE_CAP
        || !isfinite(x) || !isfinite(y) || !isfinite(horizontal_offset) || !isfinite(alpha)) {
        return;
    }


    begin_menu_text_blend();
    float cursor = horizontal_offset;
    const float baseline = y + vigia_menu_line_baseline(line);
    for (size_t i = 0U; i < line->count; i++) {
        const int cp = line->glyphs[i];
        if (cp == VIGIA_MENU_DOT || cp == VIGIA_MENU_DASH) {
            draw_separator(cp, x + cursor, baseline, rgba, alpha);
        } else {
            const int ascii = cp >= VIGIA_ASCII_FIRST && cp <= VIGIA_ASCII_LAST ? cp : '?';
            DrawTextCodepoint(draw->font, ascii,
                (Vector2){ .x = roundf(x) + floorf(cursor), .y = roundf(baseline - 14.0F) },
                (float)VIGIA_FONT_SIZE, vigia_color(rgba, alpha));
        }
        cursor += vigia_menu_glyph_advance(cp);
    }
    BeginBlendMode(BLEND_ALPHA_PREMULTIPLY);
}

static unsigned int style_rgba(VigiaTextStyle style)
{
    switch (style) {
    case VIGIA_TEXT_LABEL:
    case VIGIA_TEXT_MUTED:
        return VIGIA_DIM_RGBA;
    case VIGIA_TEXT_REVERSE:
        return VIGIA_SURFACE_RGBA;
    case VIGIA_TEXT_CONTENT:
    default:
        return VIGIA_FOREGROUND_RGBA;
    }
}

static float style_spacing(VigiaTextStyle style)
{
    return style == VIGIA_TEXT_LABEL ? (float)VIGIA_TRACKING_LABEL
                                     : (float)VIGIA_TRACKING_CONTENT;
}

Color vigia_color(unsigned int rgba, float alpha)
{
    const float clamped = alpha < 0.0F ? 0.0F : (alpha > 1.0F ? 1.0F : alpha);
    const unsigned int base = (rgba & 0xFFU);
    return (Color){
        .r = (unsigned char)((rgba >> 24U) & 0xFFU),
        .g = (unsigned char)((rgba >> 16U) & 0xFFU),
        .b = (unsigned char)((rgba >> 8U) & 0xFFU),
        .a = (unsigned char)((float)base * clamped),
    };
}

static Rectangle to_rectangle(VigiaRect rect)
{
    return (Rectangle){
        .x = (float)rect.x,
        .y = (float)rect.y,
        .width = (float)rect.width,
        .height = (float)rect.height,
    };
}

bool vigia_draw_init(VigiaDraw *draw)
{
    if (draw == NULL) {
        return false;
    }
    *draw = (VigiaDraw){0};

    const char *const font_path = getenv("VIGIA_FONT");
    if (font_path == NULL || font_path[0] == '\0') {
        (void)fputs("vigia-ray: VIGIA_FONT is unset; deserted is required\n",
                    stderr);
        return false;
    }

    int codepoints[VIGIA_ASCII_COUNT] = {0};
    for (int codepoint = VIGIA_ASCII_FIRST;
         codepoint <= VIGIA_ASCII_LAST; codepoint++) {
        codepoints[codepoint - VIGIA_ASCII_FIRST] = codepoint;
    }

    const Font font = LoadFontEx(font_path, VIGIA_FONT_SIZE, codepoints,
                                 (int)(sizeof codepoints / sizeof codepoints[0]));
    if (!IsFontValid(font)) {
        (void)fprintf(stderr, "vigia-ray: could not load font %s\n", font_path);
        return false;
    }


    if (font.baseSize != VIGIA_FONT_SIZE) {
        (void)fprintf(stderr,
                      "vigia-ray: font strike is %dpx, expected %dpx\n",
                      font.baseSize, VIGIA_FONT_SIZE);
        UnloadFont(font);
        return false;
    }

    draw->font = font;
    draw->owns_font = true;
    return true;
}

void vigia_draw_shutdown(VigiaDraw *draw)
{
    if (draw == NULL) {
        return;
    }
    if (draw->owns_font) {
        UnloadFont(draw->font);
    }
    *draw = (VigiaDraw){0};
}

void vigia_draw_surface(VigiaRect rect, float alpha)
{
    if (rect.width <= 0 || rect.height <= 0) {
        return;
    }
    DrawRectangleRec(to_rectangle(rect), vigia_color(VIGIA_SURFACE_RGBA, alpha));
    DrawRectangleLinesEx(to_rectangle(rect), 1.0F,
                         vigia_color(VIGIA_KEYLINE_RGBA, alpha));
}

void vigia_draw_reverse(const VigiaDraw *draw, VigiaRect rect,
                        const char *text, float alpha)
{
    if (draw == NULL || rect.width <= 0 || rect.height <= 0) {
        return;
    }
    DrawRectangleRec(to_rectangle(rect),
                     vigia_color(VIGIA_FOREGROUND_RGBA, alpha));
    if (text == NULL || text[0] == '\0') {
        return;
    }
    const int cells = vigia_text_cells(text, VIGIA_TEXT_REVERSE);
    const int x = rect.x + (rect.width - cells * VIGIA_CELL_W) / 2;
    DrawTextEx(draw->font, text, (Vector2){ .x = (float)x, .y = (float)rect.y },
               (float)VIGIA_FONT_SIZE, 0.0F,
               vigia_color(VIGIA_SURFACE_RGBA, alpha));
}

void vigia_draw_text_tinted(const VigiaDraw *draw, VigiaRect content, int col,
                            int row, const char *text, VigiaTextStyle style,
                            unsigned int rgba, float alpha)
{
    if (draw == NULL || text == NULL || text[0] == '\0') {
        return;
    }
    const VigiaRect cell = vigia_cell_rect(content, col, row, 1);
    DrawTextEx(draw->font, text,
               (Vector2){ .x = (float)cell.x, .y = (float)cell.y },
               (float)VIGIA_FONT_SIZE, style_spacing(style),
               vigia_color(rgba, alpha));
}

void vigia_draw_text(const VigiaDraw *draw, VigiaRect content, int col,
                     int row, const char *text, VigiaTextStyle style,
                     float alpha)
{
    vigia_draw_text_tinted(draw, content, col, row, text, style,
                           style_rgba(style), alpha);
}

void vigia_draw_text_right(const VigiaDraw *draw, VigiaRect content,
                           int cols_from_right, int row, const char *text,
                           VigiaTextStyle style, float alpha)
{
    if (text == NULL) {
        return;
    }



    const int cells = vigia_text_cells(text, style);
    const int cols = content.width / VIGIA_CELL_W;
    vigia_draw_text(draw, content, cols - cols_from_right - cells, row, text,
                    style, alpha);
}

void vigia_draw_text_centre(const VigiaDraw *draw, VigiaRect content, int row,
                            const char *text, VigiaTextStyle style,
                            float alpha)
{
    if (text == NULL) {
        return;
    }

    const int cells = vigia_text_cells(text, style);
    const int cols = content.width / VIGIA_CELL_W;
    vigia_draw_text(draw, content, (cols - cells) / 2, row, text, style, alpha);
}
