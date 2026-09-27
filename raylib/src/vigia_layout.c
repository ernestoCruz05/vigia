#include "vigia_layout.h"
#include "vigia_theme.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>

static float clamp_progress(float progress)
{
    if (!isfinite(progress) || progress < 0.0F) {
        return 0.0F;
    }
    if (progress > 1.0F) {
        return 1.0F;
    }
    return progress;
}


static int centered_offset(int parent, int child)
{

    return (parent / 2 + parent % 2) - (child / 2 + child % 2);
}

static int bounded_toast_count(size_t toast_count)
{
    const size_t bounded = toast_count > VIGIA_MAX_HISTORY
        ? VIGIA_MAX_HISTORY
        : toast_count;
    return (int)bounded;
}


VigiaLayout vigia_layout_compute(int output_width, int output_height,
                                 float scale, float progress,
                                 bool recalling, bool osd_visible,
                                 size_t toast_count)
{
    (void)scale;
    (void)recalling;

    VigiaLayout layout = {0};
    if (output_width <= 0 || output_height <= 0) {
        return layout;
    }

    const int menu_width = output_width / 2;
    const int menu_x = centered_offset(output_width, menu_width);
    const float bounded_progress = clamp_progress(progress);
    const int clip_width = (int)round((double)menu_width * (double)bounded_progress);
    const int clip_offset = centered_offset(menu_width, clip_width);
    const int toast_count_int = bounded_toast_count(toast_count);

    layout.menu = (VigiaRect){
        .x = menu_x,
        .y = VIGIA_MENU_TOP,
        .width = menu_width,
        .height = VIGIA_MENU_HEIGHT,
    };
    layout.menu_clip = (VigiaRect){
        .x = menu_x + clip_offset,
        .y = VIGIA_MENU_TOP,
        .width = clip_width,
        .height = VIGIA_MENU_HEIGHT,
    };
    layout.menu_draw_offset = clip_offset - (menu_width - clip_width) / 2;
    layout.osd = (VigiaRect){
        .x = centered_offset(output_width, VIGIA_OSD_WIDTH),
        .y = VIGIA_MENU_TOP,
        .width = VIGIA_OSD_WIDTH,
        .height = VIGIA_OSD_HEIGHT,
    };
    layout.osd_clip = layout.osd;
    layout.toast = (VigiaRect){
        .x = output_width - VIGIA_TOAST_MARGIN - VIGIA_TOAST_WIDTH,
        .y = VIGIA_TOAST_MARGIN,
        .width = VIGIA_TOAST_WIDTH,
        .height = VIGIA_TOAST_HEIGHT_PLAIN,
    };
    layout.toast_stack_height = toast_count_int * VIGIA_TOAST_HEIGHT_PLAIN;
    if (toast_count_int > 1) {
        layout.toast_stack_height += (toast_count_int - 1) * VIGIA_TOAST_GAP;
    }
    layout.recall = (VigiaRect){
        .x = layout.toast.x,
        .y = layout.toast.y,
        .width = VIGIA_TOAST_WIDTH,
        .height = layout.toast_stack_height + VIGIA_RECALL_ROW_HEIGHT,
    };



    if (!osd_visible) {
        layout.osd_placement = VIGIA_OSD_NOWHERE;
    } else if (bounded_progress > 0.0F) {
        layout.osd_placement = VIGIA_OSD_BORROW;
    } else {
        layout.osd_placement = VIGIA_OSD_PILL;
    }
    return layout;
}

void vigia_layout_osd_progress(VigiaLayout *layout, float progress)
{
    if (layout == NULL) {
        return;
    }
    const int width = (int)roundf((float)layout->osd.width * clamp_progress(progress));
    layout->osd_clip = layout->osd;
    layout->osd_clip.width = width;
    layout->osd_clip.x += centered_offset(layout->osd.width, width);
    if (layout->menu_clip.width == 0 && width > 0
        && layout->osd_placement == VIGIA_OSD_NOWHERE) {
        layout->osd_placement = VIGIA_OSD_PILL;
    }
}

static bool vigia_rects_overlap_vertically(VigiaRect first, VigiaRect second)
{
    const int64_t first_bottom = (int64_t)first.y + (int64_t)first.height;
    const int64_t second_bottom = (int64_t)second.y + (int64_t)second.height;
    return first.height > 0 && second.height > 0
        && (int64_t)first.y < second_bottom && (int64_t)second.y < first_bottom;
}

typedef struct {
    int start;
    int end;
} VigiaInterval;

static bool vigia_layout_transient_interval(VigiaRect menu, VigiaRect transient,
                                            VigiaInterval *interval)
{
    if (interval == NULL || !vigia_rects_overlap_vertically(menu, transient)) {
        return false;
    }

    const int64_t menu_start = menu.x;
    const int64_t menu_end = menu_start + (int64_t)menu.width;
    const int64_t transient_start = transient.x;
    const int64_t transient_end = transient_start + (int64_t)transient.width;
    const int64_t start = transient_start > menu_start ? transient_start : menu_start;
    const int64_t end = transient_end < menu_end ? transient_end : menu_end;
    if (end <= start) {
        return false;
    }
    interval->start = (int)start;
    interval->end = (int)end;
    return true;
}

static void vigia_layout_order_intervals(VigiaInterval *first,
                                         VigiaInterval *second)
{
    if (first != NULL && second != NULL && second->start < first->start) {
        const VigiaInterval temporary = *first;
        *first = *second;
        *second = temporary;
    }
}

size_t vigia_layout_input_regions(const VigiaLayout *layout,
                                  VigiaPhase phase, bool recalling,
                                  bool osd_visible,
                                  size_t active_toast_count,
                                  VigiaRect *regions, size_t capacity)
{
    if (layout == NULL || regions == NULL || capacity == 0U) {
        return 0U;
    }

    (void)osd_visible;
    size_t region_count = 0U;
    if (phase == VIGIA_REVEALED) {
        VigiaInterval intervals[2] = {0};
        size_t interval_count = 0U;
        if (layout->osd_placement == VIGIA_OSD_PILL
            && vigia_layout_transient_interval(layout->menu,
                                                            layout->osd_clip,
                                                            &intervals[interval_count])) {
            interval_count++;
        }
        const int toast_count = bounded_toast_count(active_toast_count);
        if (toast_count > 0) {
            VigiaRect toast_stack = layout->toast;
            toast_stack.height = toast_count * VIGIA_TOAST_HEIGHT_PLAIN
                + (toast_count - 1) * VIGIA_TOAST_GAP;
            if (vigia_layout_transient_interval(layout->menu, toast_stack,
                                                 &intervals[interval_count])) {
                interval_count++;
            }
        }
        if (interval_count == 2U) {
            vigia_layout_order_intervals(&intervals[0], &intervals[1]);
        }

        int segment_start = layout->menu.x;
        const int menu_end = layout->menu.x + layout->menu.width;
        for (size_t index = 0U; index < interval_count; index++) {
            if (segment_start < intervals[index].start && region_count < capacity) {
                regions[region_count++] = (VigiaRect){
                    .x = segment_start,
                    .y = layout->menu.y,
                    .width = intervals[index].start - segment_start,
                    .height = layout->menu.height,
                };
            }
            if (intervals[index].end > segment_start) {
                segment_start = intervals[index].end;
            }
        }
        if (segment_start < menu_end && region_count < capacity) {
            regions[region_count++] = (VigiaRect){
                .x = segment_start,
                .y = layout->menu.y,
                .width = menu_end - segment_start,
                .height = layout->menu.height,
            };
        }
    }

    if (recalling && region_count < capacity) {
        regions[region_count++] = layout->recall;
    }
    return region_count;
}


bool vigia_rect_contains(VigiaRect rect, int x, int y)
{
    const int64_t delta_x = (int64_t)x - (int64_t)rect.x;
    const int64_t delta_y = (int64_t)y - (int64_t)rect.y;
    return rect.width > 0 && rect.height > 0
        && delta_x >= 0 && delta_x < (int64_t)rect.width
        && delta_y >= 0 && delta_y < (int64_t)rect.height;
}


VigiaClickTarget vigia_layout_click_target(const VigiaLayout *layout,
                                           VigiaPhase phase, bool recalling,
                                           bool osd_visible,
                                           size_t active_toast_count,
                                           int x, int y)
{
    if (layout == NULL) {
        return VIGIA_CLICK_NONE;
    }
    (void)osd_visible;
    if (layout->osd_placement == VIGIA_OSD_PILL
        && vigia_rect_contains(layout->osd_clip, x, y)) {
        return VIGIA_CLICK_TRANSIENT;
    }

    const int count = bounded_toast_count(active_toast_count);
    VigiaRect stack = layout->toast;
    if (count > 0) {
        stack.height = count * VIGIA_TOAST_HEIGHT_PLAIN + (count - 1) * VIGIA_TOAST_GAP;
        if (vigia_rect_contains(stack, x, y)) {
            return VIGIA_CLICK_TRANSIENT;
        }
    }
    if (recalling && vigia_rect_contains(layout->recall, x, y)) {
        return VIGIA_CLICK_RECALL;
    }
    if (phase == VIGIA_REVEALED && vigia_rect_contains(layout->menu, x, y)) {
        return VIGIA_CLICK_MENU;
    }
    return VIGIA_CLICK_NONE;
}

VigiaRect vigia_content(VigiaRect surface)
{
    const int width = surface.width - VIGIA_PAD_X * 2;
    const int height = surface.height - VIGIA_PAD_Y * 2;
    return (VigiaRect){
        .x = surface.x + VIGIA_PAD_X,
        .y = surface.y + VIGIA_PAD_Y,
        .width = width > 0 ? width : 0,
        .height = height > 0 ? height : 0,
    };
}


VigiaRect vigia_cell_rect(VigiaRect content, int col, int row, int cols)
{
    return (VigiaRect){
        .x = content.x + col * VIGIA_CELL_W,
        .y = content.y + row * VIGIA_CELL_H,
        .width = cols * VIGIA_CELL_W,
        .height = VIGIA_CELL_H,
    };
}

int vigia_toast_height(size_t body_lines)
{
    const size_t bounded = body_lines > (size_t)VIGIA_TOAST_BODY_MAX_LINES
        ? (size_t)VIGIA_TOAST_BODY_MAX_LINES
        : body_lines;
    return VIGIA_SURFACE_HEIGHT(2 + (int)bounded);
}

VigiaRect vigia_layout_tag(VigiaRect menu, size_t index)
{
    if (index >= VIGIA_WORKSPACE_CAP || menu.width <= 0 || menu.height <= 0) {
        return (VigiaRect){0};
    }
    const int64_t x = (int64_t)menu.x + 2 * (int64_t)VIGIA_MENU_PADDING
        + (int64_t)index * VIGIA_MENU_TAG_WIDTH;
    if (x < INT_MIN || x > INT_MAX) {
        return (VigiaRect){0};
    }
    return (VigiaRect){ .x = (int)x, .y = menu.y,
        .width = VIGIA_MENU_TAG_WIDTH, .height = menu.height };
}


VigiaMenuZones vigia_layout_menu_zones(VigiaRect menu, size_t workspace_count,
                                      float clock_text_width)
{
    if (menu.width <= 0 || menu.height <= 0) {
        return (VigiaMenuZones){0};
    }
    const size_t count = workspace_count < VIGIA_WORKSPACE_CAP
        ? workspace_count : VIGIA_WORKSPACE_CAP;
    const float text_width = isfinite(clock_text_width) && clock_text_width > 0.0F
        ? fminf(clock_text_width, (float)menu.width) : 0.0F;
    const float padding = (float)VIGIA_MENU_PADDING;
    const float tag_component = 2.0F * padding
        + (float)count * (float)VIGIA_MENU_TAG_WIDTH;
    const float clock_component = text_width + 2.0F * padding;
    const float side = fmaxf(tag_component, clock_component)
        + (float)VIGIA_MENU_REGION_GAP;
    const float centre_width = fmaxf(0.0F, (float)menu.width - 2.0F * side);
    VigiaRect tags = vigia_layout_tag(menu, 0U);
    tags.width = (int)count * VIGIA_MENU_TAG_WIDTH;
    return (VigiaMenuZones){
        .tags = tags,
        .centre = { .x = (float)menu.x + floorf(((float)menu.width - centre_width) / 2.0F),
            .y = (float)menu.y, .width = centre_width, .height = (float)menu.height },
        .clock = { .x = (float)menu.x + (float)menu.width - 2.0F * padding - text_width,
            .y = (float)menu.y, .width = text_width, .height = (float)menu.height },
    };
}


VigiaHit vigia_layout_hit(const VigiaLayout *layout, VigiaPhase phase,
                          bool recalling, size_t workspace_count,
                          const int *row_heights, size_t row_count,
                          int x, int y)
{
    const VigiaHit none = { .kind = VIGIA_HIT_NONE, .index = 0U };
    if (layout == NULL) {
        return none;
    }

    if (recalling) {
        int row_y = layout->recall.y;
        for (size_t index = 0U; index < row_count && row_heights != NULL;
             index++) {
            const VigiaRect row = {
                .x = layout->recall.x,
                .y = row_y,
                .width = layout->recall.width,
                .height = row_heights[index],
            };
            if (vigia_rect_contains(row, x, y)) {
                return (VigiaHit){ .kind = VIGIA_HIT_RECALL_ROW,
                                   .index = index };
            }
            row_y += row_heights[index] + VIGIA_TOAST_GAP;
        }
        const VigiaRect clear = {
            .x = layout->recall.x,
            .y = row_y,
            .width = layout->recall.width,
            .height = VIGIA_RECALL_ROW_HEIGHT,
        };
        if (vigia_rect_contains(clear, x, y)) {
            return (VigiaHit){ .kind = VIGIA_HIT_CLEAR_ALL, .index = 0U };
        }
    }

    if (phase != VIGIA_REVEALED) {
        return none;
    }
    if (!vigia_rect_contains(layout->menu, x, y)) {
        return none;
    }
    const size_t bounded = workspace_count > VIGIA_WORKSPACE_CAP
        ? (size_t)VIGIA_WORKSPACE_CAP
        : workspace_count;
    for (size_t index = 0U; index < bounded; index++) {
        const VigiaRect tag = vigia_layout_tag(layout->menu, index);
        if (vigia_rect_contains(tag, x, y)) {
            return (VigiaHit){ .kind = VIGIA_HIT_TAG, .index = index };
        }
    }
    return none;
}
