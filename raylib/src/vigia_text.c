#include "vigia_text.h"

#include <string.h>

#define VIGIA_TEXT_ASCII_FIRST 32
#define VIGIA_TEXT_ASCII_LAST 126



static const char latin1_fold[] =
    "AAAAAAACEEEEIIII"
    "DNOOOOO?OUUUUY??"
    "aaaaaaaceeeeiiii"
    "dnooooo?ouuuuy?y";

static bool decode_utf8(const char **cursor, unsigned int *codepoint)
{
    const unsigned char *bytes = (const unsigned char *)*cursor;
    if (bytes[0] == 0U) {
        return false;
    }

    unsigned int value = 0U;
    size_t length = 0U;
    if (bytes[0] < 0x80U) {
        value = bytes[0];
        length = 1U;
    } else if ((bytes[0] & 0xE0U) == 0xC0U) {
        value = (unsigned int)(bytes[0] & 0x1FU);
        length = 2U;
    } else if ((bytes[0] & 0xF0U) == 0xE0U) {
        value = (unsigned int)(bytes[0] & 0x0FU);
        length = 3U;
    } else if ((bytes[0] & 0xF8U) == 0xF0U) {
        value = (unsigned int)(bytes[0] & 0x07U);
        length = 4U;
    } else {


        *cursor += 1;
        *codepoint = 0xFFFDU;
        return true;
    }

    for (size_t index = 1U; index < length; index++) {
        if ((bytes[index] & 0xC0U) != 0x80U) {
            *cursor += 1;
            *codepoint = 0xFFFDU;
            return true;
        }
        value = (value << 6U) | (unsigned int)(bytes[index] & 0x3FU);
    }
    *cursor += length;
    *codepoint = value;
    return true;
}

bool vigia_text_fold(char *out, size_t capacity, const char *in)
{
    if (out == NULL || capacity == 0U) {
        return false;
    }
    if (in == NULL) {
        out[0] = '\0';
        return true;
    }




    size_t written = 0U;
    const char *cursor = in;
    unsigned int codepoint = 0U;
    while (decode_utf8(&cursor, &codepoint)) {
        char folded = '?';
        if (codepoint >= (unsigned int)VIGIA_TEXT_ASCII_FIRST
            && codepoint <= (unsigned int)VIGIA_TEXT_ASCII_LAST) {
            folded = (char)codepoint;
        } else if (codepoint >= 0xC0U && codepoint <= 0xFFU) {
            folded = latin1_fold[codepoint - 0xC0U];
        }
        if (written + 1U >= capacity) {
            out[written] = '\0';
            return false;
        }
        out[written++] = folded;
    }
    out[written] = '\0';
    return true;
}

int vigia_text_cells(const char *text, VigiaTextStyle style)
{
    if (text == NULL || text[0] == '\0') {
        return 0;
    }

    const int length = (int)strlen(text);
    if (style != VIGIA_TEXT_LABEL) {
        return length;
    }

    return length * 2 - 1;
}

void vigia_text_upper(char *text)
{
    if (text == NULL) {
        return;
    }
    for (size_t index = 0U; text[index] != '\0'; index++) {
        if (text[index] >= 'a' && text[index] <= 'z') {
            text[index] = (char)(text[index] - ('a' - 'A'));
        }
    }
}

size_t vigia_text_max_chars(int cells, VigiaTextStyle style)
{
    if (cells <= 0) {
        return 0U;
    }
    if (style != VIGIA_TEXT_LABEL) {
        return (size_t)cells;
    }
    return (size_t)((cells + 1) / 2);
}

void vigia_text_truncate(char *text, size_t max_chars)
{
    if (text == NULL) {
        return;
    }

    const size_t length = strlen(text);
    if (length <= max_chars) {
        return;
    }
    if (max_chars < 4U) {
        text[max_chars] = '\0';
        return;
    }
    text[max_chars - 3U] = '.';
    text[max_chars - 2U] = '.';
    text[max_chars - 1U] = '.';
    text[max_chars] = '\0';
}

size_t vigia_text_line_count(const char *text, size_t max_chars,
                             size_t max_lines)
{
    if (text == NULL || text[0] == '\0') {
        return 0U;
    }
    char line[VIGIA_TEXT_LINE_CAP] = {0};
    const char *cursor = text;
    size_t count = 0U;
    while (count < max_lines
           && vigia_text_wrap_next(&cursor, max_chars, line, sizeof line)) {
        count++;
    }
    return count;
}

bool vigia_text_wrap_next(const char **cursor, size_t max_chars,
                          char *out, size_t capacity)
{
    if (cursor == NULL || *cursor == NULL || out == NULL || capacity == 0U
        || max_chars == 0U) {
        return false;
    }

    const char *start = *cursor;
    while (*start == ' ') {
        start++;
    }
    if (*start == '\0') {
        *cursor = start;
        return false;
    }



    size_t take = 0U;
    size_t last_space = 0U;
    while (start[take] != '\0' && take < max_chars) {
        if (start[take] == ' ') {
            last_space = take;
        }
        take++;
    }
    if (start[take] != '\0' && last_space > 0U) {
        take = last_space;
    }
    if (take >= capacity) {
        take = capacity - 1U;
    }

    memcpy(out, start, take);
    out[take] = '\0';
    *cursor = start + take;
    return true;
}
