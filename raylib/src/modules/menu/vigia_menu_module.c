#include "vigia_host.h"
#include "vigia_module.h"
#include "vigia_clock.h"
#include "vigia_wayland.h"
#include "vigia_workspace.h"

#include <stdlib.h>

typedef struct {
    VigiaHost *host;
    bool enabled;
    bool clock_valid;
    time_t timestamp;
    VigiaWorkspace *workspaces;
} VigiaMenuModule;

static bool menu_init(VigiaModule *self, VigiaHost *host)
{
    VigiaMenuModule *mod = calloc(1U, sizeof(VigiaMenuModule));
    if (mod == NULL) {
        return false;
    }
    mod->host = host;
    mod->enabled = host->config.menu_enabled;
    if (!host->demo) {
        vigia_workspace_update(NULL, &host->state);
        host->state.clock[0] = '\0';
    }
    self->state = mod;
    return true;
}

static void menu_shutdown(VigiaModule *self)
{
    VigiaMenuModule *mod = self->state;
    if (mod != NULL) {
        vigia_workspace_destroy(mod->workspaces);
        free(mod);
    }
    self->state = NULL;
}

static bool menu_prepare_config(VigiaModule *self, const VigiaConfig *candidate)
{
    (void)self;
    (void)candidate;
    return true;
}

static void menu_apply_config(VigiaModule *self, const VigiaConfig *config)
{
    VigiaMenuModule *mod = self->state;
    if (mod != NULL) {
        if (mod->enabled != config->menu_enabled) {
            mod->clock_valid = false;
        }
        mod->enabled = config->menu_enabled;
        if (!mod->enabled) {
            vigia_workspace_destroy(mod->workspaces);
            mod->workspaces = NULL;
            if (!mod->host->demo) {
                vigia_workspace_update(NULL, &mod->host->state);
                mod->host->state.clock[0] = '\0';
            }
        }
    }
}

static void menu_update(VigiaModule *self, float delta_time)
{
    (void)delta_time;
    VigiaMenuModule *mod = self->state;
    if (mod == NULL || !mod->enabled || mod->host->demo) {
        return;
    }
    const time_t now = time(NULL);
    if (!mod->clock_valid || now != mod->timestamp) {
        if (now == (time_t)-1
            || !vigia_clock_format(now, mod->host->state.clock, sizeof mod->host->state.clock)) {
            mod->host->state.clock[0] = '\0';
        }
        mod->timestamp = now;
        mod->clock_valid = true;
        if (mod->workspaces == NULL) {
            mod->workspaces = vigia_workspace_create(vigia_wayland_get_display(),
                                                     vigia_wayland_get_output());
        }
    }
    vigia_workspace_update(mod->workspaces, &mod->host->state);
}

static size_t menu_get_input_regions(const VigiaModule *self, VigiaRect *regions, size_t capacity)
{
    (void)self;
    (void)regions;
    (void)capacity;
    return 0U;
}

static void menu_render(VigiaModule *self, VigiaDraw *draw)
{
    (void)self;
    (void)draw;
}

static bool menu_handle_pointer(VigiaModule *self, int x, int y, bool is_down, bool is_released)
{
    (void)self;
    (void)x;
    (void)y;
    (void)is_down;
    (void)is_released;
    return false;
}

VigiaModule *vigia_menu_module_create(void)
{
    VigiaModule *mod = calloc(1U, sizeof(VigiaModule));
    if (mod == NULL) {
        return NULL;
    }
    mod->name = "menu";
    mod->init = menu_init;
    mod->shutdown = menu_shutdown;
    mod->prepare_config = menu_prepare_config;
    mod->apply_config = menu_apply_config;
    mod->update = menu_update;
    mod->get_input_regions = menu_get_input_regions;
    mod->render = menu_render;
    mod->handle_pointer = menu_handle_pointer;
    return mod;
}
