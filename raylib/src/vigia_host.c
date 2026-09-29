#include "vigia_host.h"
#include "vigia_render.h"
#include "vigia_text.h"
#include "vigia_window.h"

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

VigiaHost *vigia_host_create(VigiaDraw *draw)
{
    VigiaHost *host = calloc(1U, sizeof(VigiaHost));
    if (host == NULL) {
        return NULL;
    }
    host->draw = draw;
    host->screen_width = 1920;
    host->screen_height = 1080;
    host->frame_valid = true;
    vigia_state_init(&host->state);
    return host;
}

bool vigia_host_register(VigiaHost *host, VigiaModule *module)
{
    if (host == NULL || module == NULL || host->module_count >= VIGIA_HOST_MAX_MODULES) {
        return false;
    }
    host->modules[host->module_count++] = module;
    return true;
}

static void abort_config(VigiaHost *host)
{
    for (size_t i = 0U; i < host->module_count; i++) {
        if (host->modules[i]->abort_config != NULL) {
            host->modules[i]->abort_config(host->modules[i]);
        }
    }
}

static bool prepare_config(VigiaHost *host, const VigiaConfig *candidate)
{
    for (size_t i = 0U; i < host->module_count; i++) {
        VigiaModule *module = host->modules[i];
        if (module->prepare_config != NULL && !module->prepare_config(module, candidate)) {
            (void)fprintf(stderr, "vigia-ray: module %s rejected candidate config\n", module->name);
            abort_config(host);
            return false;
        }
    }
    return true;
}

static void apply_config(VigiaHost *host, VigiaConfig *candidate)
{
    host->presentation_valid = false;
    VigiaConfig previous = host->config;
    host->config = *candidate;
    *candidate = (VigiaConfig){0};
    for (size_t i = 0U; i < host->module_count; i++) {
        if (host->modules[i]->apply_config != NULL) {
            host->modules[i]->apply_config(host->modules[i], &host->config);
        }
    }
    vigia_config_free(&previous);
}

static bool watch_assets(VigiaWatcher *watcher, const VigiaConfig *config)
{
    if (config != NULL && config->launcher.enabled) {
        for (size_t i = 0U; i < config->launcher.app_count; i++) {
            if (!vigia_watcher_add_path(watcher, config->launcher.apps[i].icon)) {
                return false;
            }
        }
    }
    return true;
}

static VigiaWatcher *config_watches(const VigiaHost *host, const VigiaConfig *candidate, bool retain_previous)
{
    VigiaWatcher *watcher = vigia_watcher_create();
    if (watcher == NULL) {
        return NULL;
    }
    if (!vigia_watcher_add_path(watcher, host->config_path) || !watch_assets(watcher, candidate)
        || (retain_previous && !watch_assets(watcher, &host->config))) {
        vigia_watcher_destroy(watcher);
        return NULL;
    }
    return watcher;
}

bool vigia_host_init(VigiaHost *host, const char *explicit_config)
{
    if (host == NULL) {
        return false;
    }
    if (explicit_config != NULL && explicit_config[0] != '\0') {
        const int length = snprintf(host->config_path, sizeof host->config_path, "%s", explicit_config);
        if (length < 0 || (size_t)length >= sizeof host->config_path) {
            return false;
        }
    } else if (!vigia_config_resolve_default_path(host->config_path, sizeof host->config_path)) {
        return false;
    }
    host->config = (VigiaConfig){ .version = 1, .menu_enabled = true, .osd_enabled = true,
        .notifications_enabled = true, .launcher = { .icon_size = 24, .hover_delay_ms = 200, .leave_delay_ms = 350 },
        .music = { .display_ms = 5000 } };
    for (size_t i = 0U; i < host->module_count; i++) {
        if (host->modules[i]->init != NULL && !host->modules[i]->init(host->modules[i], host)) {
            return false;
        }
    }
    VigiaConfig candidate = {0};
    char error[256] = {0};
    const bool parsed = vigia_config_load_default(&candidate, host->config_path, error, sizeof error);
    if (!parsed) {
        (void)fprintf(stderr, "vigia-ray: config error: %s\n", error);
    }
    host->watcher = config_watches(host, parsed ? &candidate : NULL, false);
    if (host->watcher == NULL) {
        vigia_config_free(&candidate);
        return false;
    }
    if (parsed && prepare_config(host, &candidate)) {
        apply_config(host, &candidate);
    } else {
        for (size_t i = 0U; i < host->module_count; i++) {
            if (host->modules[i]->apply_config != NULL) {
                host->modules[i]->apply_config(host->modules[i], &host->config);
            }
        }
    }
    vigia_config_free(&candidate);
    return true;
}

void vigia_host_check_reload(VigiaHost *host)
{
    if (host == NULL || host->watcher == NULL || !vigia_watcher_check_modified(host->watcher)) {
        return;
    }
    VigiaConfig candidate = {0};
    char error[256] = {0};
    if (!vigia_config_load_file(host->config_path, &candidate, error, sizeof error)) {
        (void)fprintf(stderr, "vigia-ray: reload failed: %s\n", error);
        return;
    }
    VigiaWatcher *recovery = config_watches(host, &candidate, true);
    VigiaWatcher *committed = config_watches(host, &candidate, false);
    if (recovery == NULL || committed == NULL) {
        vigia_watcher_destroy(recovery);
        vigia_watcher_destroy(committed);
        vigia_config_free(&candidate);
        (void)fprintf(stderr, "vigia-ray: could not prepare configuration watches\n");
        return;
    }
    VigiaWatcher *previous = host->watcher;
    if (prepare_config(host, &candidate)) {
        apply_config(host, &candidate);
        host->watcher = committed;
        vigia_watcher_destroy(recovery);
    } else {
        host->watcher = recovery;
        vigia_watcher_destroy(committed);
    }
    vigia_watcher_destroy(previous);
    vigia_config_free(&candidate);
}

static void media_field(char *output, size_t capacity, const char *input)
{
    while (isspace((unsigned char)*input)) {
        input++;
    }
    size_t length = strlen(input);
    while (length > 0U && isspace((unsigned char)input[length - 1U])) {
        length--;
    }
    char trimmed[VIGIA_MPRIS_TEXT_CAP] = {0};
    if (length >= sizeof trimmed) {
        output[0] = '\0';
        return;
    }
    memcpy(trimmed, input, length);
    if (!vigia_text_fold(output, capacity, trimmed)) {
        output[capacity - 1U] = '\0';
    }
}

static void media_changed(const VigiaSpotifyTrack *track, void *data)
{
    VigiaHost *host = data;
    if (track == NULL) {
        host->state.media[0] = '\0';
        return;
    }
    char artist[128] = {0};
    char title[128] = {0};
    media_field(artist, sizeof artist, track->artist);
    media_field(title, sizeof title, track->title);
    const int length = artist[0] != '\0' && title[0] != '\0'
        ? snprintf(host->state.media, sizeof host->state.media, "%s\t%s", artist, title)
        : snprintf(host->state.media, sizeof host->state.media, "%s", title[0] != '\0' ? title : artist);
    if (length < 0 || (size_t)length >= sizeof host->state.media) {
        host->state.media[0] = '\0';
    }
}

static void update_media(VigiaHost *host)
{
    if (host->demo) {
        return;
    }
    if (!host->config.menu_enabled && !host->config.music.enabled) {
        vigia_mpris_destroy(host->mpris);
        host->mpris = NULL;
        host->state.media[0] = '\0';
        return;
    }
    if (host->mpris == NULL) {
        host->mpris = vigia_mpris_create(NULL, NULL);
    }
    vigia_mpris_set_media_callback(host->mpris,
        host->config.menu_enabled ? media_changed : NULL, host);
    if (!host->config.menu_enabled) {
        host->state.media[0] = '\0';
    }
    vigia_mpris_poll(host->mpris);
}

static void update_layout(VigiaHost *host)
{
    const int sw = IsWindowReady() ? GetScreenWidth() : (host->screen_width > 0 ? host->screen_width : 1920);
    const int sh = IsWindowReady() ? GetScreenHeight() : (host->screen_height > 0 ? host->screen_height : 1080);
    host->layout = vigia_layout_compute(
        sw, sh, 1.0F, host->state.reveal.value,
        host->state.recalling, host->state.osd.visible,
        host->state.recalling ? host->state.history_count : host->state.active_count);
    vigia_layout_osd_progress(&host->layout, host->state.osd_pill.value);
}

void vigia_host_update(VigiaHost *host, float delta_time)
{
    if (host == NULL) {
        return;
    }
    vigia_window_reap_children();
    if (!host->config.menu_enabled) {
        host->state.phase = VIGIA_HIDDEN;
        host->state.reveal = (VigiaReveal){0};
    }
    if (!host->config.osd_enabled) {
        host->state.osd = (VigiaOsd){0};
        host->state.osd_pill = (VigiaReveal){0};
        host->state.osd_hold_remaining = 0.0F;
    }
    if (!host->config.notifications_enabled) {
        host->state.recalling = false;
        host->state.active_count = 0U;
    }
    vigia_state_update(&host->state, delta_time);
    update_layout(host);

    for (size_t i = 0U; i < host->module_count; i++) {
        if (host->modules[i]->update != NULL) {
            host->modules[i]->update(host->modules[i], delta_time);
        }
    }
    update_media(host);
    update_layout(host);
}

size_t vigia_host_collect_input_regions(VigiaHost *host, Rectangle *regions, size_t capacity)
{
    if (host == NULL || regions == NULL || capacity == 0U) {
        return 0U;
    }
    update_layout(host);
    VigiaRect rects[VIGIA_LAYOUT_MAX_INPUT_REGIONS] = {0};
    size_t count = 0U;
    const size_t limit = capacity < VIGIA_LAYOUT_MAX_INPUT_REGIONS
        ? capacity : VIGIA_LAYOUT_MAX_INPUT_REGIONS;
    host->frame_valid = true;

    if (host->config.menu_enabled || host->config.notifications_enabled || host->config.osd_enabled) {
        count = vigia_layout_input_regions(
            &host->layout, host->config.menu_enabled ? host->state.phase : VIGIA_HIDDEN,
            host->config.notifications_enabled && host->state.recalling,
            host->config.osd_enabled && host->state.osd.visible,
            host->config.notifications_enabled && !host->state.recalling ? host->state.active_count : 0U,
            rects, VIGIA_LAYOUT_MAX_INPUT_REGIONS);
    }

    for (size_t m = 0U; m < host->module_count; m++) {
        if (host->modules[m]->get_input_regions != NULL) {
            VigiaRect contribution[VIGIA_LAYOUT_MAX_INPUT_REGIONS] = {0};
            const size_t added = host->modules[m]->get_input_regions(
                host->modules[m], contribution, VIGIA_LAYOUT_MAX_INPUT_REGIONS);
            if (added > VIGIA_LAYOUT_MAX_INPUT_REGIONS - count) {
                host->frame_valid = false;
                return 0U;
            }
            memcpy(&rects[count], contribution, added * sizeof contribution[0]);
            count += added;
        }
    }
    if (count > limit) {
        host->frame_valid = false;
        return 0U;
    }

    for (size_t i = 0U; i < count; i++) {
        regions[i] = (Rectangle){
            .x = (float)rects[i].x,
            .y = (float)rects[i].y,
            .width = (float)rects[i].width,
            .height = (float)rects[i].height,
        };
    }
    return count;
}

bool vigia_host_needs_redraw(VigiaHost *host)
{
    if (host == NULL) {
        return false;
    }
    const int width = IsWindowReady() ? GetScreenWidth() : host->screen_width;
    const int height = IsWindowReady() ? GetScreenHeight() : host->screen_height;
    bool changed = !host->presentation_valid || host->presented_width != width
        || host->presented_height != height || host->presented_frame_valid != host->frame_valid
        || host->presented_notifications != host->config.notifications_enabled
        || !vigia_render_equal(&host->state, &host->presented, host->config.notifications_enabled);
    for (size_t i = 0U; i < host->module_count; i++) {
        VigiaModule *module = host->modules[i];
        if (module->presentation_changed != NULL && module->presentation_changed(module)) {
            changed = true;
        }
    }
    host->presented = host->state;
    host->presented_width = width;
    host->presented_height = height;
    host->presented_frame_valid = host->frame_valid;
    host->presented_notifications = host->config.notifications_enabled;
    host->presentation_valid = true;
    return changed;
}

void vigia_host_render(VigiaHost *host)
{
    if (host == NULL) {
        return;
    }
    BeginDrawing();
    ClearBackground((Color){0});
    if (!host->frame_valid) {
        return;
    }
    vigia_render_content(host->draw, &host->state, &host->layout,
                         host->config.notifications_enabled);
    for (size_t i = 0U; i < host->module_count; i++) {
        if (host->modules[i]->render != NULL) {
            host->modules[i]->render(host->modules[i], host->draw);
        }
    }
}

static size_t collect_row_heights(const VigiaState *state, int *heights, size_t capacity)
{
    const VigiaNotification *items = state->recalling ? state->history : state->active;
    const size_t item_count = state->recalling ? state->history_count : state->active_count;
    const size_t count = item_count < capacity ? item_count : capacity;
    for (size_t index = 0U; index < count; index++) {
        heights[index] = vigia_toast_height(
            vigia_text_line_count(items[index].body,
                                  (size_t)VIGIA_TOAST_CONTENT_CELLS,
                                  (size_t)VIGIA_TOAST_BODY_MAX_LINES));
    }
    return count;
}

static void commit_hit(VigiaState *state, VigiaHit hit)
{
    if (hit.kind == VIGIA_HIT_TAG) {
        return;
    }
    if (hit.kind == VIGIA_HIT_RECALL_ROW) {
        if (hit.index < state->history_count) {
            vigia_state_dismiss(state, state->history[hit.index].id);
        }
        return;
    }
    if (hit.kind == VIGIA_HIT_CLEAR_ALL) {
        state->active_count = 0U;
        vigia_state_clear_history(state);
    }
}

void vigia_host_handle_pointer(VigiaHost *host, int x, int y, bool is_down, bool is_released)
{
    if (host == NULL) {
        return;
    }
    int row_heights[VIGIA_MAX_HISTORY] = {0};
    const size_t row_count = collect_row_heights(&host->state, row_heights, VIGIA_MAX_HISTORY);
    const VigiaHit hit = vigia_layout_hit(
        &host->layout, host->config.menu_enabled ? host->state.phase : VIGIA_HIDDEN,
        host->config.notifications_enabled && host->state.recalling, host->state.workspace_count,
        row_heights, row_count, x, y);
    const VigiaHit pointer_hit = hit.kind == VIGIA_HIT_TAG
        ? (VigiaHit){ .kind = VIGIA_HIT_NONE } : hit;
    vigia_state_point(&host->state, pointer_hit, is_down);
    if (is_released) {
        commit_hit(&host->state, hit);
    }
    for (size_t i = 0U; i < host->module_count; i++) {
        if (host->modules[i]->handle_pointer != NULL) {
            if (host->modules[i]->handle_pointer(host->modules[i], x, y, is_down, is_released)) {
                break;
            }
        }
    }
}

void vigia_host_command(VigiaHost *host, VigiaCommand command)
{
    if (host == NULL) {
        return;
    }
    for (size_t i = 0U; i < host->module_count; i++) {
        if (host->modules[i] != NULL && host->modules[i]->handle_command != NULL) {
            host->modules[i]->handle_command(host->modules[i], command);
        }
    }
    if ((!host->config.menu_enabled && (command == VIGIA_COMMAND_TOGGLE
            || command == VIGIA_COMMAND_REVEAL || command == VIGIA_COMMAND_DISMISS))
        || (!host->config.osd_enabled && command == VIGIA_COMMAND_DEMO_OSD)
        || (!host->config.notifications_enabled && (command == VIGIA_COMMAND_RECALL_TOGGLE
            || command == VIGIA_COMMAND_RECALL_DISMISS || command == VIGIA_COMMAND_DEMO_TOAST))) {
        return;
    }
    vigia_state_command(&host->state, command);
}

void vigia_host_destroy(VigiaHost *host)
{
    if (host == NULL) {
        return;
    }
    for (size_t i = 0U; i < host->module_count; i++) {
        if (host->modules[i] != NULL) {
            if (host->modules[i]->shutdown != NULL) {
                host->modules[i]->shutdown(host->modules[i]);
            }
            free(host->modules[i]);
        }
    }
    host->module_count = 0U;
    vigia_mpris_destroy(host->mpris);
    host->mpris = NULL;
    if (host->watcher != NULL) {
        vigia_watcher_destroy(host->watcher);
        host->watcher = NULL;
    }
    vigia_config_free(&host->config);
    free(host);
}
