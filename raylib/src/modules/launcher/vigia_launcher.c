#include "vigia_launcher.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "vigia_host.h"
#include "vigia_theme.h"
#include "vigia_wayland.h"

static float cubic_out(float p)
{
    const float t = 1.0f - p;
    return 1.0f - t * t * t;
}

void vigia_launcher_state_init(VigiaLauncherState *state)
{
    if (state == NULL) {
        return;
    }
    *state = (VigiaLauncherState){
        .config = {
            .enabled = false,
            .icon_size = 48,
            .hover_delay_ms = 200,
            .leave_delay_ms = 350,
            .app_count = 0U,
            .apps = NULL,
        },
        .owns_config = false,
        .config_generation = 1U,
        .visible = false,
        .pinned = false,
        .hover_inhibited = false,
        .hover_in_activation = false,
        .opacity = 0.0f,
        .start_opacity = 0.0f,
        .anim_elapsed_seconds = 0.0f,
        .dwell_elapsed_seconds = 0.0f,
        .leave_elapsed_seconds = 0.0f,
        .hovered_slot = -1,
        .pressed_slot = -1,
        .pressed_generation = 0U,
        .scroll_offset = 0,
        .pointer_x = -1,
        .pointer_y = -1,
        .pointer_down = false,
        .pointer_released = false,
        .screen_width = 1920,
        .screen_height = 1080,
        .icon_cache = NULL,
        .window_service = NULL,
        .host = NULL,
    };
}

void vigia_launcher_state_cleanup(VigiaLauncherState *state)
{
    if (state == NULL) {
        return;
    }
    if (state->owns_config && state->config.apps != NULL) {
        for (size_t i = 0U; i < state->config.app_count; i++) {
            VigiaAppConfig *app = &state->config.apps[i];
            for (size_t a = 0U; a < app->argc; a++) {
                free(app->argv[a]);
            }
            for (size_t a = 0U; a < app->app_id_count; a++) {
                free(app->app_ids[a]);
            }
        }
        free(state->config.apps);
        state->config.apps = NULL;
        state->owns_config = false;
    }
    vigia_icon_cache_destroy(state->prepared_icons);
    state->prepared_icons = NULL;
    vigia_window_service_destroy(state->prepared_windows);
    state->prepared_windows = NULL;
    if (state->icon_cache != NULL) {
        vigia_icon_cache_destroy(state->icon_cache);
        state->icon_cache = NULL;
    }
    if (state->window_service != NULL) {
        vigia_window_service_destroy(state->window_service);
        state->window_service = NULL;
    }
}

VigiaRect vigia_launcher_compute_panel_rect(int screen_width, int screen_height,
                                            int slot_width, int slot_height,
                                            size_t app_count)
{
    VigiaRect r = {0, 0, 0, 0};
    if (app_count == 0U || slot_width <= 0 || slot_height <= 0) {
        return r;
    }
    const int desired_w = 24 + (int)app_count * slot_width;
    const int max_w = screen_width - 24;
    int w = desired_w;
    if (w > max_w) {
        const int available = max_w - 24;
        const int slots = available > 0 ? (available / slot_width) : 0;
        w = 24 + slots * slot_width;
    }
    r.width = w;
    r.height = slot_height;
    r.x = (screen_width - w) / 2;
    r.y = screen_height - 12 - slot_height;
    return r;
}

VigiaRect vigia_launcher_compute_strip_rect(VigiaRect panel, int screen_height)
{
    return (VigiaRect){
        .x = panel.x,
        .y = screen_height - 1,
        .width = panel.width,
        .height = 1,
    };
}

bool vigia_launcher_is_suppressed(int screen_width, int screen_height,
                                  int slot_width, int slot_height,
                                  size_t app_count)
{
    if (app_count == 0U || slot_width <= 0 || slot_height <= 0) {
        return true;
    }
    if (screen_width < 48 + slot_width || screen_height < 12 + slot_height + 12) {
        return true;
    }
    return false;
}

void vigia_launcher_toggle(VigiaLauncherState *state)
{
    if (state == NULL || !state->config.enabled) {
        return;
    }
    if (state->visible || state->opacity > 0.001f) {
        vigia_launcher_dismiss(state);
    } else {
        state->visible = true;
        state->pinned = true;
        state->hover_inhibited = false;
        state->dwell_elapsed_seconds = 0.0f;
        state->leave_elapsed_seconds = 0.0f;
        state->start_opacity = state->opacity;
        state->anim_elapsed_seconds = 0.0f;
    }
}

void vigia_launcher_dismiss(VigiaLauncherState *state)
{
    if (state == NULL || !state->config.enabled) {
        return;
    }
    state->pinned = false;
    state->visible = false;
    state->start_opacity = state->opacity;
    state->anim_elapsed_seconds = 0.0f;
    state->leave_elapsed_seconds = 0.0f;
    state->dwell_elapsed_seconds = 0.0f;
    state->hover_inhibited = true;
    state->hover_in_activation = false;
    state->hovered_slot = -1;
    state->pressed_slot = -1;
}

void vigia_launcher_handle_wheel(VigiaLauncherState *state,
                                 int screen_width, int screen_height,
                                 int wheel_delta)
{
    if (state == NULL || wheel_delta == 0 || !state->config.enabled) {
        return;
    }
    const int slot_w = state->config.icon_size + 16;
    const int slot_h = state->config.icon_size + 24;
    if (vigia_launcher_is_suppressed(screen_width, screen_height, slot_w, slot_h, state->config.app_count)) {
        return;
    }
    const VigiaRect panel = vigia_launcher_compute_panel_rect(screen_width, screen_height, slot_w, slot_h, state->config.app_count);
    if (state->pointer_x < panel.x || state->pointer_x >= panel.x + panel.width ||
        state->pointer_y < panel.y || state->pointer_y >= panel.y + panel.height) {
        return;
    }
    const int max_w = screen_width - 24;
    const int available = max_w - 24;
    const int visible_slots = available > 0 ? (available / slot_w) : 0;
    const int max_scroll = (visible_slots > 0 && (int)state->config.app_count > visible_slots)
        ? ((int)state->config.app_count - visible_slots)
        : 0;

    state->scroll_offset -= wheel_delta;
    if (state->scroll_offset < 0) {
        state->scroll_offset = 0;
    } else if (state->scroll_offset > max_scroll) {
        state->scroll_offset = max_scroll;
    }
}

static void trigger_app_action(VigiaLauncherState *state, size_t slot_index)
{
    if (state == NULL || slot_index >= state->config.app_count || state->config.apps == NULL) {
        return;
    }
    const VigiaAppConfig *app = &state->config.apps[slot_index];
    if (state->window_service == NULL || !vigia_window_service_is_ready(state->window_service)) {
        return;
    }
    const VigiaTrackedWindow *match = vigia_window_find_best_match(
        state->window_service, (const char *const *)app->app_ids, app->app_id_count);
    if (match != NULL) {
        if (vigia_window_focus(state->window_service, (const char *const *)app->app_ids, app->app_id_count, vigia_wayland_get_seat())) {
            vigia_launcher_dismiss(state);
        }
        return;
    }
    if (vigia_window_is_pending_spawn(state->window_service, app->id, state->config_generation)) {
        return;
    }
    if (!vigia_window_mark_pending_spawn(state->window_service, app->id, state->config_generation,
            (const char *const *)app->app_ids, app->app_id_count)) {
        return;
    }
    if (vigia_window_spawn((const char *const *)app->argv, app->argc)) {
        vigia_launcher_dismiss(state);
    } else {
        vigia_window_clear_pending_spawn(state->window_service, app->id);
    }
}

void vigia_launcher_handle_pointer_state(VigiaLauncherState *state,
                                        int screen_width, int screen_height,
                                        int x, int y,
                                        bool is_down, bool is_released)
{
    if (state == NULL) {
        return;
    }
    const bool pressed = is_down && !state->pointer_down;
    state->pointer_x = x;
    state->pointer_y = y;
    state->pointer_down = is_down;
    state->pointer_released = is_released;
    state->screen_width = screen_width;
    state->screen_height = screen_height;

    if (!state->config.enabled) {
        return;
    }

    const int slot_w = state->config.icon_size + 16;
    const int slot_h = state->config.icon_size + 24;
    if (vigia_launcher_is_suppressed(screen_width, screen_height, slot_w, slot_h, state->config.app_count)) {
        return;
    }

    const VigiaRect panel = vigia_launcher_compute_panel_rect(screen_width, screen_height, slot_w, slot_h, state->config.app_count);
    const VigiaRect strip = vigia_launcher_compute_strip_rect(panel, screen_height);

    const bool in_strip = (x >= strip.x && x < strip.x + strip.width &&
                           y >= strip.y && y < strip.y + strip.height);
    const bool in_panel = (x >= panel.x && x < panel.x + panel.width &&
                           y >= panel.y && y < panel.y + panel.height);

    if (!in_strip && !in_panel) {
        state->dwell_elapsed_seconds = 0.0f;
        state->pressed_slot = -1;
        if (state->hover_in_activation) {
            state->hover_inhibited = false;
            state->hover_in_activation = false;
        }
        state->hovered_slot = -1;
        if (is_released) {
            state->pressed_slot = -1;
        }
        return;
    }

    state->leave_elapsed_seconds = 0.0f;
    if (state->hover_inhibited) {
        state->hover_in_activation = true;
    }

    if (in_panel && (state->visible || state->opacity > 0.001f)) {
        int hovered = -1;
        const int viewport_left = panel.x + 12;
        const int viewport_right = panel.x + panel.width - 12;

        for (size_t i = 0U; i < state->config.app_count; i++) {
            const int slot_x = viewport_left + ((int)i - state->scroll_offset) * slot_w;
            const int slot_r = slot_x + slot_w;
            if (slot_x < viewport_left || slot_r > viewport_right) {
                continue;
            }
            if (x >= slot_x && x < slot_r && y >= panel.y && y < panel.y + panel.height) {
                hovered = (int)i;
                break;
            }
        }
        state->hovered_slot = hovered;

        if (pressed && hovered >= 0) {
            state->pressed_slot = hovered;
            state->pressed_generation = state->config_generation;
        } else if (is_released) {
            if (state->pressed_slot >= 0 && state->pressed_slot == hovered &&
                state->pressed_generation == state->config_generation) {
                trigger_app_action(state, (size_t)hovered);
            }
            state->pressed_slot = -1;
        }
    } else {
        state->hovered_slot = -1;
        if (is_released) {
            state->pressed_slot = -1;
        }
    }
}

void vigia_launcher_update_state(VigiaLauncherState *state, float delta_time,
                                int screen_width, int screen_height)
{
    if (state == NULL || !state->config.enabled) {
        return;
    }

    const int slot_w = state->config.icon_size + 16;
    const int slot_h = state->config.icon_size + 24;
    if (vigia_launcher_is_suppressed(screen_width, screen_height, slot_w, slot_h, state->config.app_count)) {
        state->visible = false;
        state->opacity = 0.0f;
        state->dwell_elapsed_seconds = 0.0f;
        state->leave_elapsed_seconds = 0.0f;
        return;
    }

    const VigiaRect panel = vigia_launcher_compute_panel_rect(screen_width, screen_height, slot_w, slot_h, state->config.app_count);
    const VigiaRect strip = vigia_launcher_compute_strip_rect(panel, screen_height);

    const bool in_strip = (state->pointer_x >= strip.x && state->pointer_x < strip.x + strip.width &&
                           state->pointer_y >= strip.y && state->pointer_y < strip.y + strip.height);
    const bool in_panel = (state->pointer_x >= panel.x && state->pointer_x < panel.x + panel.width &&
                           state->pointer_y >= panel.y && state->pointer_y < panel.y + panel.height);

    if (!in_strip && !in_panel) {
        if (state->hover_in_activation) {
            state->hover_inhibited = false;
            state->hover_in_activation = false;
        }
    } else if (state->hover_inhibited) {
        state->hover_in_activation = true;
    }

    const float hover_delay = (float)state->config.hover_delay_ms / 1000.0f;
    const float leave_delay = (float)state->config.leave_delay_ms / 1000.0f;
    const float fade_duration = 0.180f;

    bool visibility_changed = false;

    if (!state->visible) {
        state->leave_elapsed_seconds = 0.0f;
        if (in_strip && !state->hover_inhibited) {
            state->dwell_elapsed_seconds += delta_time;
            if (state->dwell_elapsed_seconds >= hover_delay) {
                state->visible = true;
                state->dwell_elapsed_seconds = 0.0f;
                state->start_opacity = state->opacity;
                state->anim_elapsed_seconds = 0.0f;
                visibility_changed = true;
            }
        } else {
            state->dwell_elapsed_seconds = 0.0f;
        }
    } else {
        state->dwell_elapsed_seconds = 0.0f;
        if (in_strip || in_panel) {
            state->leave_elapsed_seconds = 0.0f;
        } else if (!state->pinned) {
            state->leave_elapsed_seconds += delta_time;
            if (state->leave_elapsed_seconds >= leave_delay) {
                state->visible = false;
                state->leave_elapsed_seconds = 0.0f;
                state->start_opacity = state->opacity;
                state->anim_elapsed_seconds = 0.0f;
                visibility_changed = true;
            }
        }
    }

    if (!visibility_changed) {
        state->anim_elapsed_seconds += delta_time;
        float progress = state->anim_elapsed_seconds / fade_duration;
        if (progress > 1.0f) {
            progress = 1.0f;
        }
        const float factor = cubic_out(progress);
        if (state->visible) {
            state->opacity = state->start_opacity + (1.0f - state->start_opacity) * factor;
        } else {
            state->opacity = state->start_opacity * (1.0f - factor);
        }
    }
}

size_t vigia_launcher_get_input_regions(const VigiaLauncherState *state,
                                        int screen_width, int screen_height,
                                        VigiaRect *regions, size_t capacity)
{
    if (state == NULL || regions == NULL || capacity == 0U || !state->config.enabled) {
        return 0U;
    }
    const int slot_w = state->config.icon_size + 16;
    const int slot_h = state->config.icon_size + 24;
    if (vigia_launcher_is_suppressed(screen_width, screen_height, slot_w, slot_h, state->config.app_count)) {
        return 0U;
    }
    const VigiaRect panel = vigia_launcher_compute_panel_rect(screen_width, screen_height, slot_w, slot_h, state->config.app_count);
    const VigiaRect strip = vigia_launcher_compute_strip_rect(panel, screen_height);

    if (state->visible || state->opacity > 0.001f) {
        regions[0] = panel;
        if (capacity > 1U) {
            regions[1] = strip;
            return 2U;
        }
        return 1U;
    }
    regions[0] = strip;
    return 1U;
}

void vigia_launcher_render_state(VigiaLauncherState *state, VigiaDraw *draw,
                                 int screen_width, int screen_height)
{
    (void)draw;
    if (state == NULL || !state->config.enabled || !IsWindowReady()) {
        return;
    }
    const float alpha = state->opacity;
    if (alpha <= 0.001f) {
        return;
    }
    const int slot_w = state->config.icon_size + 16;
    const int slot_h = state->config.icon_size + 24;
    if (vigia_launcher_is_suppressed(screen_width, screen_height, slot_w, slot_h, state->config.app_count)) {
        return;
    }

    const VigiaRect panel = vigia_launcher_compute_panel_rect(screen_width, screen_height, slot_w, slot_h, state->config.app_count);
    vigia_draw_surface(panel, alpha);

    const int viewport_left = panel.x + 12;
    const int viewport_right = panel.x + panel.width - 12;
    const int icon_size = state->config.icon_size;

    BeginScissorMode(viewport_left, panel.y, panel.width - 24, panel.height);

    for (size_t i = 0U; i < state->config.app_count; i++) {
        const int slot_x = viewport_left + ((int)i - state->scroll_offset) * slot_w;
        const int slot_r = slot_x + slot_w;
        if (slot_x >= viewport_right || slot_r <= viewport_left) {
            continue;
        }

        const Rectangle highlight = {
            .x = (float)(slot_x + 4),
            .y = (float)(panel.y + 4),
            .width = (float)(slot_w - 8),
            .height = (float)(slot_h - 8),
        };

        if (state->pressed_slot == (int)i && state->hovered_slot == (int)i) {
            DrawRectangleRec(highlight, vigia_color(VIGIA_FOREGROUND_RGBA, 0.14f * alpha));
        } else if (state->hovered_slot == (int)i) {
            DrawRectangleRec(highlight, vigia_color(VIGIA_FOREGROUND_RGBA, 0.08f * alpha));
        }

        if (state->config.apps != NULL && state->icon_cache != NULL) {
            const VigiaAppConfig *app = &state->config.apps[i];
            if (app->icon[0] != '\0') {
                const Texture2D tex = vigia_icon_cache_find(state->icon_cache, app->icon, icon_size);
                if (tex.id > 0U) {
                    const int icon_x = slot_x + (slot_w - icon_size) / 2;
                    const int icon_y = panel.y + (slot_h - icon_size) / 2;
                    const Rectangle src = { 0.0f, 0.0f, (float)tex.width, (float)tex.height };
                    const Rectangle dst = { (float)icon_x, (float)icon_y, (float)icon_size, (float)icon_size };
                    DrawTexturePro(tex, src, dst, (Vector2){0.0f, 0.0f}, 0.0f, vigia_color(0xFFFFFFFFU, alpha));
                }
            }
        }
    }

    EndScissorMode();
}

static bool module_init(VigiaModule *self, VigiaHost *host)
{
    if (self == NULL) {
        return false;
    }
    VigiaLauncherState *state = calloc(1U, sizeof(VigiaLauncherState));
    if (state == NULL) {
        return false;
    }
    vigia_launcher_state_init(state);
    state->host = host;
    self->state = state;
    return true;
}

static void module_shutdown(VigiaModule *self)
{
    if (self == NULL || self->state == NULL) {
        return;
    }
    VigiaLauncherState *state = self->state;
    vigia_launcher_state_cleanup(state);
    free(state);
    self->state = NULL;
}

static void module_abort_config(VigiaModule *self)
{
    VigiaLauncherState *state = self != NULL ? self->state : NULL;
    if (state != NULL) {
        vigia_icon_cache_destroy(state->prepared_icons);
        vigia_window_service_destroy(state->prepared_windows);
        state->prepared_icons = NULL;
        state->prepared_windows = NULL;
        state->prepared = false;
    }
}

static bool module_prepare_config(VigiaModule *self, const VigiaConfig *candidate)
{
    VigiaLauncherState *state = self != NULL ? self->state : NULL;
    if (state == NULL || candidate == NULL) {
        return false;
    }
    module_abort_config(self);
    if (candidate->launcher.enabled && candidate->launcher.app_count > 0U) {
        state->prepared_icons = vigia_icon_cache_create(VIGIA_ICON_DEFAULT_MAX_BYTES);
        if (state->prepared_icons == NULL) {
            return false;
        }
        for (size_t i = 0U; i < candidate->launcher.app_count; i++) {
            if (vigia_icon_cache_get(state->prepared_icons, candidate->launcher.apps[i].icon,
                candidate->launcher.icon_size).id == 0U) {
                module_abort_config(self);
                return false;
            }
        }
        if (state->window_service == NULL) {
            state->prepared_windows = vigia_window_service_create(
                state->host != NULL && !state->host->demo ? vigia_wayland_get_display() : NULL);
            if (state->prepared_windows == NULL) {
                module_abort_config(self);
                return false;
            }
        }
    }
    state->prepared = true;
    return true;
}

static void module_apply_config(VigiaModule *self, const VigiaConfig *config)
{
    if (self == NULL || self->state == NULL || config == NULL) {
        return;
    }
    VigiaLauncherState *state = self->state;
    if (!state->prepared && !module_prepare_config(self, config)) {
        return;
    }
    vigia_icon_cache_destroy(state->icon_cache);
    state->icon_cache = state->prepared_icons;
    state->prepared_icons = NULL;
    if (state->prepared_windows != NULL) {
        state->window_service = state->prepared_windows;
        state->prepared_windows = NULL;
    }
    if (!config->launcher.enabled || config->launcher.app_count == 0U) {
        vigia_window_service_destroy(state->window_service);
        state->window_service = NULL;
        state->visible = false;
        state->pinned = false;
        state->opacity = 0.0F;
        state->start_opacity = 0.0F;
    }
    state->config = config->launcher;
    state->owns_config = false;
    state->config_generation++;
    state->pressed_slot = -1;
    state->hovered_slot = -1;
    state->scroll_offset = 0;
    state->dwell_elapsed_seconds = 0.0F;
    state->leave_elapsed_seconds = 0.0F;
    state->prepared = false;
}

static void module_update(VigiaModule *self, float delta_time)
{
    if (self == NULL || self->state == NULL) {
        return;
    }
    VigiaLauncherState *state = self->state;
    const int sw = IsWindowReady() ? GetScreenWidth() : state->screen_width;
    const int sh = IsWindowReady() ? GetScreenHeight() : state->screen_height;
    if (state->host != NULL) {
        state->host->launcher_footprint = (VigiaRect){0};
        if (state->config.enabled && !vigia_launcher_is_suppressed(sw, sh,
                state->config.icon_size + 16, state->config.icon_size + 24, state->config.app_count)) {
            state->host->launcher_footprint = vigia_launcher_compute_panel_rect(sw, sh,
                state->config.icon_size + 16, state->config.icon_size + 24, state->config.app_count);
        }
    }
    if (!state->config.enabled) {
        return;
    }
    if (state->window_service != NULL) {
        vigia_window_update(state->window_service, delta_time);
    }
    if (IsWindowReady()) {
        const int wheel = (int)GetMouseWheelMove();
        if (wheel != 0) {
            vigia_launcher_handle_wheel(state, sw, sh, wheel);
        }
    }
    vigia_launcher_update_state(state, delta_time, sw, sh);
}

static size_t module_get_input_regions(const VigiaModule *self, VigiaRect *regions, size_t capacity)
{
    if (self == NULL || self->state == NULL) {
        return 0U;
    }
    const VigiaLauncherState *state = self->state;
    const int sw = IsWindowReady() ? GetScreenWidth() : state->screen_width;
    const int sh = IsWindowReady() ? GetScreenHeight() : state->screen_height;
    return vigia_launcher_get_input_regions(state, sw, sh, regions, capacity);
}

static bool module_presentation_changed(VigiaModule *self)
{
    VigiaLauncherState *state = self->state;
    if (state == NULL) {
        return false;
    }
    const int width = IsWindowReady() ? GetScreenWidth() : state->screen_width;
    const int height = IsWindowReady() ? GetScreenHeight() : state->screen_height;
    const bool visible = state->config.enabled && state->opacity > 0.001F
        && !vigia_launcher_is_suppressed(width, height, state->config.icon_size + 16,
            state->config.icon_size + 24, state->config.app_count);
    const bool changed = visible != state->presented_visible || (visible
        && (state->opacity != state->presented_opacity
            || state->hovered_slot != state->presented_hovered
            || state->pressed_slot != state->presented_pressed
            || state->scroll_offset != state->presented_scroll
            || state->config_generation != state->presented_generation));
    state->presented_visible = visible;
    state->presented_opacity = state->opacity;
    state->presented_hovered = state->hovered_slot;
    state->presented_pressed = state->pressed_slot;
    state->presented_scroll = state->scroll_offset;
    state->presented_generation = state->config_generation;
    return changed;
}

static void module_render(VigiaModule *self, VigiaDraw *draw)
{
    if (self == NULL || self->state == NULL) {
        return;
    }
    VigiaLauncherState *state = self->state;
    const int sw = IsWindowReady() ? GetScreenWidth() : state->screen_width;
    const int sh = IsWindowReady() ? GetScreenHeight() : state->screen_height;
    vigia_launcher_render_state(state, draw, sw, sh);
}

static bool module_handle_pointer(VigiaModule *self, int x, int y, bool is_down, bool is_released)
{
    if (self == NULL || self->state == NULL) {
        return false;
    }
    VigiaLauncherState *state = self->state;
    const int sw = IsWindowReady() ? GetScreenWidth() : state->screen_width;
    const int sh = IsWindowReady() ? GetScreenHeight() : state->screen_height;
    vigia_launcher_handle_pointer_state(state, sw, sh, x, y, is_down, is_released);
    return state->visible || state->opacity > 0.001f;
}

static void module_handle_command(VigiaModule *self, VigiaCommand command)
{
    if (self == NULL || self->state == NULL) {
        return;
    }
    VigiaLauncherState *state = self->state;
    if (command == VIGIA_COMMAND_LAUNCHER_TOGGLE) {
        vigia_launcher_toggle(state);
    } else if (command == VIGIA_COMMAND_LAUNCHER_DISMISS) {
        vigia_launcher_dismiss(state);
    }
}

VigiaModule *vigia_launcher_module_create(void)
{
    VigiaModule *module = calloc(1U, sizeof(VigiaModule));
    if (module == NULL) {
        return NULL;
    }
    *module = (VigiaModule){
        .name = "launcher",
        .state = NULL,
        .init = module_init,
        .shutdown = module_shutdown,
        .prepare_config = module_prepare_config,
        .abort_config = module_abort_config,
        .apply_config = module_apply_config,
        .update = module_update,
        .get_input_regions = module_get_input_regions,
        .presentation_changed = module_presentation_changed,
        .render = module_render,
        .handle_pointer = module_handle_pointer,
        .handle_command = module_handle_command,
    };
    return module;
}
