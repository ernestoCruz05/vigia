#include "vigia_render.h"
#include "vigia_menu_render.h"
#include "vigia_text.h"

#include <string.h>

static size_t body_lines(const VigiaNotification *item,
                         char lines[VIGIA_TOAST_BODY_MAX_LINES][VIGIA_TEXT_CAP])
{
    if (item->body[0] == '\0') {
        return 0U;
    }
    char folded_body[VIGIA_TEXT_CAP] = {0};
    vigia_text_fold(folded_body, sizeof folded_body, item->body);
    const char *cursor = folded_body;
    size_t count = 0U;
    while (count < (size_t)VIGIA_TOAST_BODY_MAX_LINES
           && vigia_text_wrap_next(&cursor,
                                   (size_t)VIGIA_TOAST_CONTENT_CELLS,
                                   lines[count], VIGIA_TEXT_CAP)) {
        count++;
    }


    if (count > 0U && *cursor != '\0') {
        vigia_text_truncate(lines[count - 1U],
                            (size_t)VIGIA_TOAST_CONTENT_CELLS);
    }
    return count;
}

static void draw_notification(const VigiaDraw *draw,
                              const VigiaNotification *item,
                              VigiaRect rect, bool hovered, bool pressed)
{
    char lines[VIGIA_TOAST_BODY_MAX_LINES][VIGIA_TEXT_CAP] = {0};
    const size_t count = body_lines(item, lines);
    const unsigned int accent = item->critical ? VIGIA_WARNING_RGBA
                                                : VIGIA_FOREGROUND_RGBA;

    DrawRectangleRec((Rectangle){ .x = (float)rect.x, .y = (float)rect.y,
                                  .width = (float)rect.width,
                                  .height = (float)rect.height },
                     vigia_color(VIGIA_SURFACE_RGBA, 1.0F));

    const unsigned int keyline = item->critical ? VIGIA_WARNING_RGBA
        : ((hovered || pressed) ? VIGIA_FOREGROUND_RGBA : VIGIA_KEYLINE_RGBA);
    DrawRectangleLinesEx((Rectangle){ .x = (float)rect.x, .y = (float)rect.y,
                                      .width = (float)rect.width,
                                      .height = (float)rect.height },
                         1.0F, vigia_color(keyline, 1.0F));

    char app_name[VIGIA_TEXT_CAP] = {0};
    char time[VIGIA_TEXT_CAP] = {0};
    char summary[VIGIA_TEXT_CAP] = {0};
    vigia_text_fold(app_name, sizeof app_name, item->app_name);
    vigia_text_fold(time, sizeof time, item->time);
    vigia_text_fold(summary, sizeof summary, item->summary);


    vigia_text_upper(app_name);

    const VigiaRect content = vigia_content(rect);
    vigia_draw_text(draw, content, 0, 0, app_name, VIGIA_TEXT_MUTED, 1.0F);
    vigia_draw_text_right(draw, content, 0, 0, time, VIGIA_TEXT_MUTED, 1.0F);
    vigia_draw_text_tinted(draw, content, 0, 1, summary,
                           VIGIA_TEXT_CONTENT, accent, 1.0F);
    for (size_t index = 0U; index < count; index++) {
        const VigiaTextStyle style = pressed ? VIGIA_TEXT_CONTENT
                                             : VIGIA_TEXT_MUTED;
        vigia_draw_text(draw, content, 0, 2 + (int)index, lines[index], style,
                        1.0F);
    }
}

static void draw_notifications(const VigiaDraw *draw, const VigiaState *state,
                               const VigiaLayout *layout)
{
    const VigiaNotification *items = state->recalling ? state->history
                                                      : state->active;
    const size_t item_count = state->recalling ? state->history_count
                                               : state->active_count;
    const size_t count = item_count > VIGIA_MAX_HISTORY
        ? (size_t)VIGIA_MAX_HISTORY
        : item_count;

    int y = layout->toast.y;
    for (size_t index = 0U; index < count; index++) {
        char lines[VIGIA_TOAST_BODY_MAX_LINES][VIGIA_TEXT_CAP] = {0};
        const int height = vigia_toast_height(body_lines(&items[index], lines));
        const VigiaHit here = { .kind = VIGIA_HIT_RECALL_ROW, .index = index };

        const bool hovered = state->recalling
            && vigia_hit_equal(state->hovered, here);
        const bool pressed = state->recalling
            && vigia_hit_equal(state->pressed, here);
        draw_notification(draw, &items[index],
                          (VigiaRect){ .x = layout->toast.x, .y = y,
                                       .width = layout->toast.width,
                                       .height = height },
                          hovered, pressed);
        y += height + VIGIA_TOAST_GAP;
    }

    if (!state->recalling) {
        return;
    }

    const VigiaRect clear = { .x = layout->recall.x, .y = y,
                              .width = layout->recall.width,
                              .height = VIGIA_RECALL_ROW_HEIGHT };
    const VigiaHit here = { .kind = VIGIA_HIT_CLEAR_ALL, .index = 0U };
    if (vigia_hit_equal(state->pressed, here)) {
        vigia_draw_reverse(draw, clear, "CLEAR ALL", 1.0F);
        return;
    }
    vigia_draw_surface(clear, 1.0F);
    const VigiaTextStyle style = vigia_hit_equal(state->hovered, here)
        ? VIGIA_TEXT_CONTENT
        : VIGIA_TEXT_LABEL;
    vigia_draw_text_centre(draw, vigia_content(clear), 0, "CLEAR ALL", style,
                           1.0F);
}

void vigia_render_frame(const VigiaDraw *draw, const VigiaState *state,
                        const VigiaLayout *layout)
{
    if (draw == NULL || state == NULL || layout == NULL) {
        return;
    }

    BeginDrawing();
    ClearBackground((Color){ 0, 0, 0, 0 });
    vigia_render_content(draw, state, layout, true);
}

void vigia_render_content(const VigiaDraw *draw, const VigiaState *state,
                          const VigiaLayout *layout, bool notifications_enabled)
{
    if (draw == NULL || state == NULL || layout == NULL) {
        return;
    }
    vigia_render_menu_osd(draw, state, layout);
    if (notifications_enabled) {
        draw_notifications(draw, state, layout);
    }
}

static bool notification_equal(const VigiaNotification *a, const VigiaNotification *b)
{
    return a->critical == b->critical
        && strcmp(a->app_name, b->app_name) == 0
        && strcmp(a->summary, b->summary) == 0
        && strcmp(a->body, b->body) == 0
        && strcmp(a->time, b->time) == 0;
}

bool vigia_render_equal(const VigiaState *a, const VigiaState *b, bool notifications)
{
    if (a->reveal.value != b->reveal.value || a->osd_pill.value != b->osd_pill.value
        || a->osd.visible != b->osd.visible) {
        return false;
    }
    if (a->reveal.value > 0.0F) {
        if (strcmp(a->clock, b->clock) != 0 || strcmp(a->media, b->media) != 0
            || a->workspace_count != b->workspace_count
            || a->tag_visual_count != b->tag_visual_count) {
            return false;
        }
        for (size_t i = 0U; i < a->workspace_count; i++) {
            if (a->workspace_ids[i] != b->workspace_ids[i]) {
                return false;
            }
        }
        for (size_t i = 0U; i < a->tag_visual_count; i++) {
            if (a->tag_visuals[i].workspace_id != b->tag_visuals[i].workspace_id
                || a->tag_visuals[i].height != b->tag_visuals[i].height
                || a->tag_visuals[i].rgba != b->tag_visuals[i].rgba) {
                return false;
            }
        }
    }
    if (a->osd.visible || a->osd_pill.value > 0.0F) {
        if (a->osd.value != b->osd.value || a->osd.has_fill != b->osd.has_fill
            || a->osd.warning != b->osd.warning
            || strcmp(a->osd.label, b->osd.label) != 0
            || strcmp(a->osd.message, b->osd.message) != 0) {
            return false;
        }
    }
    if (!notifications) {
        return true;
    }
    if (a->recalling != b->recalling) {
        return false;
    }
    if (a->recalling && (!vigia_hit_equal(a->hovered, b->hovered)
        || !vigia_hit_equal(a->pressed, b->pressed))) {
        return false;
    }
    const size_t count = a->recalling ? a->history_count : a->active_count;
    if (count != (b->recalling ? b->history_count : b->active_count)) {
        return false;
    }
    const VigiaNotification *first = a->recalling ? a->history : a->active;
    const VigiaNotification *second = b->recalling ? b->history : b->active;
    for (size_t i = 0U; i < count; i++) {
        if (!notification_equal(&first[i], &second[i])) {
            return false;
        }
    }
    return true;
}
