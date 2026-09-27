#ifndef VIGIA_LAUNCHER_H
#define VIGIA_LAUNCHER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vigia_config.h"
#include "vigia_draw.h"
#include "vigia_icon.h"
#include "vigia_layout.h"
#include "vigia_module.h"
#include "vigia_window.h"

typedef struct VigiaLauncherState {
    bool presented_visible;
    float presented_opacity;
    int presented_hovered;
    int presented_pressed;
    int presented_scroll;
    uint64_t presented_generation;
    VigiaLauncherConfig config;
    bool owns_config;
    uint64_t config_generation;
    bool visible;
    bool pinned;
    bool hover_inhibited;
    bool hover_in_activation;
    float opacity;
    float start_opacity;
    float anim_elapsed_seconds;
    float dwell_elapsed_seconds;
    float leave_elapsed_seconds;
    int hovered_slot;
    int pressed_slot;
    uint64_t pressed_generation;
    int scroll_offset;
    int pointer_x;
    int pointer_y;
    bool pointer_down;
    bool pointer_released;
    int screen_width;
    int screen_height;
    VigiaIconCache *icon_cache;
    VigiaIconCache *prepared_icons;
    VigiaWindowService *prepared_windows;
    bool prepared;
    VigiaWindowService *window_service;
    VigiaHost *host;
} VigiaLauncherState;

void vigia_launcher_state_init(VigiaLauncherState *state);
void vigia_launcher_state_cleanup(VigiaLauncherState *state);

VigiaRect vigia_launcher_compute_panel_rect(int screen_width, int screen_height,
                                            int slot_width, int slot_height,
                                            size_t app_count);

VigiaRect vigia_launcher_compute_strip_rect(VigiaRect panel, int screen_height);

bool vigia_launcher_is_suppressed(int screen_width, int screen_height,
                                  int slot_width, int slot_height,
                                  size_t app_count);

void vigia_launcher_toggle(VigiaLauncherState *state);
void vigia_launcher_dismiss(VigiaLauncherState *state);

void vigia_launcher_handle_pointer_state(VigiaLauncherState *state,
                                        int screen_width, int screen_height,
                                        int x, int y,
                                        bool is_down, bool is_released);

void vigia_launcher_handle_wheel(VigiaLauncherState *state,
                                 int screen_width, int screen_height,
                                 int wheel_delta);

void vigia_launcher_update_state(VigiaLauncherState *state, float delta_time,
                                int screen_width, int screen_height);

size_t vigia_launcher_get_input_regions(const VigiaLauncherState *state,
                                        int screen_width, int screen_height,
                                        VigiaRect *regions, size_t capacity);

void vigia_launcher_render_state(VigiaLauncherState *state, VigiaDraw *draw,
                                 int screen_width, int screen_height);

VigiaModule *vigia_launcher_module_create(void);

#endif
