#include "vigia_audio.h"
#include "vigia_host.h"
#include "vigia_module.h"

#include <stdlib.h>
#include <stdio.h>

typedef struct {
    VigiaHost *host;
    bool enabled;
    VigiaAudio *audio;
} VigiaOsdModule;

static void audio_changed(unsigned int volume, bool muted, void *data)
{
    VigiaOsdModule *mod = data;
    VigiaOsd payload = {.label = "VOL", .value = muted ? 0 : (int)volume,
        .visible = true, .has_fill = true, .warning = muted};
    const int length = muted
        ? snprintf(payload.message, sizeof payload.message, "MUTE")
        : snprintf(payload.message, sizeof payload.message, "%u", volume);
    if (length >= 0 && (size_t)length < sizeof payload.message) {
        (void)vigia_state_show_osd(&mod->host->state, &payload);
    }
}

static bool osd_init(VigiaModule *self, VigiaHost *host)
{
    VigiaOsdModule *mod = calloc(1U, sizeof(VigiaOsdModule));
    if (mod == NULL) {
        return false;
    }
    mod->host = host;
    mod->enabled = host->config.osd_enabled;
    self->state = mod;
    return true;
}

static void osd_shutdown(VigiaModule *self)
{
    VigiaOsdModule *mod = self->state;
    if (mod != NULL) {
        vigia_audio_destroy(mod->audio);
    }
    free(self->state);
    self->state = NULL;
}

static bool osd_prepare_config(VigiaModule *self, const VigiaConfig *candidate)
{
    (void)self;
    (void)candidate;
    return true;
}

static void osd_apply_config(VigiaModule *self, const VigiaConfig *config)
{
    VigiaOsdModule *mod = self->state;
    if (mod != NULL) {
        mod->enabled = config->osd_enabled;
        if (!mod->enabled || mod->host->demo) {
            vigia_audio_destroy(mod->audio);
            mod->audio = NULL;
        }
    }
}

static void osd_update(VigiaModule *self, float delta_time)
{
    (void)delta_time;
    VigiaOsdModule *mod = self->state;
    if (mod == NULL || !mod->enabled || mod->host->demo) {
        return;
    }
    if (mod->audio == NULL) {
        mod->audio = vigia_audio_create(audio_changed, mod);
    }
    vigia_audio_poll(mod->audio);
}

static size_t osd_get_input_regions(const VigiaModule *self, VigiaRect *regions, size_t capacity)
{
    (void)self;
    (void)regions;
    (void)capacity;
    return 0U;
}

static void osd_render(VigiaModule *self, VigiaDraw *draw)
{
    (void)self;
    (void)draw;
}

static bool osd_handle_pointer(VigiaModule *self, int x, int y, bool is_down, bool is_released)
{
    (void)self;
    (void)x;
    (void)y;
    (void)is_down;
    (void)is_released;
    return false;
}

VigiaModule *vigia_osd_module_create(void)
{
    VigiaModule *mod = calloc(1U, sizeof(VigiaModule));
    if (mod == NULL) {
        return NULL;
    }
    mod->name = "osd";
    mod->init = osd_init;
    mod->shutdown = osd_shutdown;
    mod->prepare_config = osd_prepare_config;
    mod->apply_config = osd_apply_config;
    mod->update = osd_update;
    mod->get_input_regions = osd_get_input_regions;
    mod->render = osd_render;
    mod->handle_pointer = osd_handle_pointer;
    return mod;
}
