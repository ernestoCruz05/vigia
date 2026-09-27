#ifndef VIGIA_LAYOUT_H
#define VIGIA_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>

#include "vigia_model.h"
#include "vigia_theme.h"

typedef struct {
    int x;
    int y;
    int width;
    int height;
} VigiaRect;



typedef struct {
    float x;
    float y;
    float width;
    float height;
} VigiaBox;

typedef struct {
    VigiaRect tags;
    VigiaBox centre;
    VigiaBox clock;
} VigiaMenuZones;

VigiaRect vigia_layout_tag(VigiaRect menu, size_t index);
VigiaMenuZones vigia_layout_menu_zones(VigiaRect menu, size_t workspace_count,
                                      float clock_text_width);

typedef enum {
    VIGIA_CLICK_NONE,
    VIGIA_CLICK_MENU,
    VIGIA_CLICK_RECALL,
    VIGIA_CLICK_TRANSIENT
} VigiaClickTarget;

typedef enum {
    VIGIA_OSD_NOWHERE,
    VIGIA_OSD_PILL,
    VIGIA_OSD_BORROW
} VigiaOsdPlacement;

#define VIGIA_LAYOUT_MAX_INPUT_REGIONS 16U

typedef struct {
    VigiaRect menu;
    VigiaRect menu_clip;
    int menu_draw_offset;
    VigiaRect osd;
    VigiaRect osd_clip;
    VigiaRect toast;
    VigiaRect recall;
    int toast_stack_height;
    VigiaOsdPlacement osd_placement;
} VigiaLayout;

VigiaLayout vigia_layout_compute(int output_width, int output_height,
                                 float scale, float progress,
                                 bool recalling, bool osd_visible,
                                 size_t toast_count);

void vigia_layout_osd_progress(VigiaLayout *layout, float progress);
size_t vigia_layout_input_regions(const VigiaLayout *layout,
                                  VigiaPhase phase, bool recalling,
                                  bool osd_visible,
                                  size_t active_toast_count,
                                  VigiaRect *regions, size_t capacity);
bool vigia_rect_contains(VigiaRect rect, int x, int y);
VigiaClickTarget vigia_layout_click_target(const VigiaLayout *layout,
                                           VigiaPhase phase, bool recalling,
                                           bool osd_visible,
                                           size_t active_toast_count,
                                           int x, int y);


VigiaRect vigia_content(VigiaRect surface);


VigiaRect vigia_cell_rect(VigiaRect content, int col, int row, int cols);


int vigia_toast_height(size_t body_lines);

VigiaHit vigia_layout_hit(const VigiaLayout *layout, VigiaPhase phase,
                          bool recalling, size_t workspace_count,
                          const int *row_heights, size_t row_count,
                          int x, int y);

#endif
