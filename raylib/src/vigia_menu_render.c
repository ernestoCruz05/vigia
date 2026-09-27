#include "vigia_menu_render.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>

static float content_opacity(float value)
{
    return fminf(1.0F, fmaxf(0.0F, (value - VIGIA_CONTENT_FADE_START)
                                  / (1.0F - VIGIA_CONTENT_FADE_START)));
}

static void begin_clip(VigiaRect clip)
{
    BeginScissorMode(clip.x, clip.y, clip.width, clip.height);
}

static bool clip_zone(VigiaRect outer, VigiaBox zone)
{
    const double left = fmax((double)outer.x, floor((double)zone.x));
    const double top = fmax((double)outer.y, floor((double)zone.y));
    const double right = fmin((double)outer.x + outer.width,
                              ceil((double)zone.x + (double)zone.width));
    const double bottom = fmin((double)outer.y + outer.height,
                               ceil((double)zone.y + (double)zone.height));
    if (!isfinite(left) || !isfinite(top) || !isfinite(right) || !isfinite(bottom)
        || right <= left || bottom <= top || left < INT_MIN || left > INT_MAX
        || top < INT_MIN || top > INT_MAX || right - left > INT_MAX
        || bottom - top > INT_MAX) {
        return false;
    }
    BeginScissorMode((int)left, (int)top, (int)(right - left), (int)(bottom - top));
    return true;
}

static float text_top(VigiaBox band, const VigiaMenuLine *line, bool optical)
{
    return band.y + roundf(band.height / 2.0F)
        - roundf(vigia_menu_line_height(line) / 2.0F) + (optical ? 2.0F : 0.0F);
}

static VigiaBox to_box(VigiaRect rect)
{
    return (VigiaBox){ .x = (float)rect.x, .y = (float)rect.y,
        .width = (float)rect.width, .height = (float)rect.height };
}

static void draw_surface(VigiaRect rect, bool warning)
{
    const Color keyline = warning ? vigia_menu_color(VIGIA_WARNING_RGBA, 1.0F)
        : vigia_menu_color(VIGIA_FOREGROUND_RGBA, 0.55F);
    const Rectangle surface = { .x = (float)rect.x, .y = (float)rect.y,
        .width = (float)rect.width, .height = (float)rect.height };
    if (rect.width <= 2 || rect.height <= 2) {
        DrawRectangleRec(surface, keyline);
        return;
    }

    DrawRectangleRec((Rectangle){ .x = surface.x + 1.0F, .y = surface.y + 1.0F,
        .width = surface.width - 2.0F, .height = surface.height - 2.0F },
        vigia_menu_color(VIGIA_SURFACE_RGBA, 1.0F));
    DrawRectangleLinesEx(surface, 1.0F, keyline);
}

static void draw_tags(const VigiaDraw *draw, const VigiaState *state,
                      const VigiaLayout *layout, VigiaMenuZones zones, float alpha)
{
    const VigiaBox zone = to_box(zones.tags);
    if (!clip_zone(layout->menu_clip, zone)) {
        return;
    }
    const size_t count = state->workspace_count < VIGIA_WORKSPACE_CAP
        ? state->workspace_count : VIGIA_WORKSPACE_CAP;
    for (size_t i = 0U; i < count; i++) {
        const int id = state->workspace_ids[i];
        const VigiaTagVisual *const visual = vigia_state_tag_visual(state, id);
        if (visual == NULL) {
            continue;
        }
        const VigiaRect slot = vigia_layout_tag(layout->menu, i);
        DrawRectangleRec((Rectangle){ .x = (float)slot.x + 11.0F, .y = (float)slot.y,
            .width = (float)VIGIA_MENU_SPINE_WIDTH, .height = visual->height },
            vigia_menu_color(visual->rgba, alpha));
        char label[16] = {0};

        const int written = snprintf(label, sizeof label, "%02d", id);
        if (written < 0 || (size_t)written >= sizeof label) {
            continue;
        }
        VigiaMenuLine line = {0};
        (void)vigia_menu_line_literal(&line, label);
        vigia_menu_line_elide(&line, (float)slot.width);
        const float x = (float)slot.x + floorf(((float)slot.width - line.width) / 2.0F);
        vigia_draw_menu_line(draw, &line, x, text_top(to_box(slot), &line, true),
                             0.0F, visual->rgba, alpha);
    }
    begin_clip(layout->menu_clip);
}

static void draw_osd_payload(const VigiaDraw *draw, const VigiaState *state,
                             VigiaBox band, VigiaRect outer, float alpha)
{
    if (!state->osd.visible || !clip_zone(outer, band)) {
        return;
    }
    VigiaMenuLine label = {0};
    VigiaMenuLine message = {0};
    (void)vigia_menu_line_literal(&label, state->osd.label);
    (void)vigia_menu_line_literal(&message, state->osd.message);
    vigia_menu_line_elide(&label, band.width);
    const float message_x = band.x + label.width + (float)VIGIA_MENU_REGION_GAP;
    vigia_menu_line_elide(&message, band.x + band.width - message_x);
    vigia_draw_menu_line(draw, &label, band.x, text_top(band, &label, false), 0.0F,
                         state->osd.warning ? VIGIA_WARNING_RGBA : VIGIA_DIM_RGBA, alpha);
    vigia_draw_menu_line(draw, &message, message_x, text_top(band, &message, false), 0.0F,
                         state->osd.warning ? VIGIA_WARNING_RGBA : VIGIA_FOREGROUND_RGBA, alpha);
    begin_clip(outer);
}

static void draw_menu(const VigiaDraw *draw, const VigiaState *state,
                      const VigiaLayout *layout)
{
    if (layout->menu_clip.width <= 0 || layout->menu_clip.height <= 0) {
        return;
    }
    const float alpha = content_opacity(state->reveal.value);
    VigiaMenuLine clock = {0};
    (void)vigia_menu_line_pair(&clock, state->clock, VIGIA_MENU_DOT);
    vigia_menu_line_elide(&clock, fmaxf(0.0F, (float)layout->menu.width - 48.0F));
    const VigiaMenuZones zones = vigia_layout_menu_zones(layout->menu,
                                                         state->workspace_count, clock.width);
    begin_clip(layout->menu_clip);
    draw_surface(layout->menu, false);
    draw_tags(draw, state, layout, zones, alpha);
    if (clip_zone(layout->menu_clip, zones.clock)) {
        vigia_draw_menu_line(draw, &clock, zones.clock.x,
                             text_top(zones.clock, &clock, true), 0.0F, VIGIA_FOREGROUND_RGBA, alpha);
        begin_clip(layout->menu_clip);
    }
    if (layout->osd_placement == VIGIA_OSD_BORROW) {
        draw_osd_payload(draw, state, zones.centre, layout->menu_clip, alpha);
    } else if (zones.centre.width > 0.0F && clip_zone(layout->menu_clip, zones.centre)) {
        VigiaMenuLine media = {0};
        (void)vigia_menu_line_pair(&media, state->media, VIGIA_MENU_DASH);
        vigia_menu_line_elide(&media, zones.centre.width);
        vigia_draw_menu_line(draw, &media, zones.centre.x,
            text_top(zones.centre, &media, true), (zones.centre.width - media.width) / 2.0F,
            VIGIA_FOREGROUND_RGBA, alpha);
    }
    EndScissorMode();
}

static void draw_pill(const VigiaDraw *draw, const VigiaState *state,
                      const VigiaLayout *layout)
{
    if (layout->osd_clip.width <= 0 || layout->osd_clip.height <= 0) {
        return;
    }
    begin_clip(layout->osd_clip);
    draw_surface(layout->osd, state->osd.warning);
    const VigiaBox box = to_box(layout->osd);
    if (state->osd.has_fill) {
        const int value = state->osd.value < 0 ? 0 : (state->osd.value > 100 ? 100 : state->osd.value);
        const float width = box.width * ((float)value / 100.0F);
        if (width > 0.0F) {
            DrawRectangleRec((Rectangle){ .x = box.x, .y = box.y,
                .width = width, .height = box.height }, vigia_menu_color(VIGIA_FOREGROUND_RGBA, 0.16F));
            DrawRectangleRec((Rectangle){ .x = box.x + width - 1.0F, .y = box.y,
                .width = 1.0F, .height = box.height }, vigia_menu_color(VIGIA_FOREGROUND_RGBA, 0.55F));
        }
    }
    const VigiaBox content = { .x = box.x + VIGIA_MENU_PADDING, .y = box.y,
        .width = fmaxf(0.0F, box.width - 2.0F * VIGIA_MENU_PADDING), .height = box.height };
    draw_osd_payload(draw, state, content, layout->osd_clip, 1.0F);
    EndScissorMode();
}

void vigia_render_menu_osd(const VigiaDraw *draw, const VigiaState *state,
                           const VigiaLayout *layout)
{
    if (draw == NULL || state == NULL || layout == NULL) {
        return;
    }
    BeginBlendMode(BLEND_ALPHA_PREMULTIPLY);
    VigiaLayout presented = *layout;
    presented.menu.x += layout->menu_draw_offset;
    draw_menu(draw, state, &presented);
    if (layout->osd_placement == VIGIA_OSD_PILL) {
        draw_pill(draw, state, layout);
    }
    EndBlendMode();
}
