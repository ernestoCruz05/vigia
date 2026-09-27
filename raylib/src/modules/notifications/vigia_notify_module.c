#include "vigia_host.h"
#include "vigia_notifications.h"
#include "vigia_text.h"
#include "vigia_module.h"

#include <stdlib.h>

typedef struct {
    VigiaHost *host;
    bool enabled;
    VigiaNotifications *service;
} VigiaNotifyModule;

static bool notify_init(VigiaModule *self, VigiaHost *host)
{
    VigiaNotifyModule *mod = calloc(1U, sizeof(VigiaNotifyModule));
    if (mod == NULL) {
        return false;
    }
    mod->host = host;
    mod->enabled = host->config.notifications_enabled;
    self->state = mod;
    return true;
}

static void notify_shutdown(VigiaModule *self)
{
    VigiaNotifyModule *mod = self->state;
    if (mod != NULL) {
        vigia_notifications_destroy(mod->service);
    }
    free(self->state);
    self->state = NULL;
}

static bool notify_prepare_config(VigiaModule *self, const VigiaConfig *candidate)
{
    (void)self;
    (void)candidate;
    return true;
}

static void notify_apply_config(VigiaModule *self, const VigiaConfig *config)
{
    VigiaNotifyModule *mod = self->state;
    if (mod != NULL) {
        mod->enabled = config->notifications_enabled;
        if (!mod->enabled || mod->host->demo) {
            vigia_notifications_destroy(mod->service);
            mod->service = NULL;
        }
    }
}

static void notify_update(VigiaModule *self, float delta_time)
{
    (void)delta_time;
    VigiaNotifyModule *mod = self->state;
    if (mod == NULL || !mod->enabled || mod->host->demo) {
        return;
    }
    if (mod->service == NULL) {
        mod->service = vigia_notifications_create(&mod->host->state);
    }
    vigia_notifications_poll(mod->service);
}

static size_t notify_get_input_regions(const VigiaModule *self, VigiaRect *regions, size_t capacity)
{
    (void)self;
    (void)regions;
    (void)capacity;
    return 0U;
}

static void notify_render(VigiaModule *self, VigiaDraw *draw)
{
    (void)self;
    (void)draw;
}

static bool notify_handle_pointer(VigiaModule *self, int x, int y, bool is_down, bool is_released)
{
    (void)is_down;
    VigiaNotifyModule *mod = self->state;
    if (mod == NULL || !mod->enabled || !is_released || mod->host->state.recalling) {
        return false;
    }
    VigiaState *state = &mod->host->state;
    VigiaRect row = mod->host->layout.toast;
    for (size_t i = 0U; i < state->active_count; i++) {
        row.height = vigia_toast_height(vigia_text_line_count(state->active[i].body,
            (size_t)VIGIA_TOAST_CONTENT_CELLS, (size_t)VIGIA_TOAST_BODY_MAX_LINES));
        if (vigia_rect_contains(row, x, y)) {
            vigia_state_dismiss(state, state->active[i].id);
            vigia_notifications_poll(mod->service);
            return true;
        }
        row.y += row.height + VIGIA_TOAST_GAP;
    }
    return false;
}

VigiaModule *vigia_notify_module_create(void)
{
    VigiaModule *mod = calloc(1U, sizeof(VigiaModule));
    if (mod == NULL) {
        return NULL;
    }
    mod->name = "notifications";
    mod->init = notify_init;
    mod->shutdown = notify_shutdown;
    mod->prepare_config = notify_prepare_config;
    mod->apply_config = notify_apply_config;
    mod->update = notify_update;
    mod->get_input_regions = notify_get_input_regions;
    mod->render = notify_render;
    mod->handle_pointer = notify_handle_pointer;
    return mod;
}
