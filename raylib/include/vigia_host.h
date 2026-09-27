#ifndef VIGIA_HOST_H
#define VIGIA_HOST_H

#include <stdbool.h>
#include <stddef.h>

#include "raylib.h"
#include "vigia_config.h"
#include "vigia_control.h"
#include "vigia_draw.h"
#include "vigia_layout.h"
#include "vigia_model.h"
#include "vigia_module.h"
#include "vigia_mpris.h"
#include "vigia_watcher.h"

#define VIGIA_HOST_MAX_MODULES 8U

struct VigiaHost {
    VigiaConfig config;
    char config_path[VIGIA_CONFIG_PATH_MAX];
    VigiaWatcher *watcher;
    VigiaMpris *mpris;
    VigiaState state;
    VigiaState presented;
    bool presentation_valid;
    bool presented_frame_valid;
    bool presented_notifications;
    int presented_width;
    int presented_height;
    VigiaLayout layout;
    VigiaRect launcher_footprint;
    VigiaDraw *draw;
    size_t module_count;
    VigiaModule *modules[VIGIA_HOST_MAX_MODULES];
    int screen_width;
    int screen_height;
    bool frame_valid;
    bool demo;
};

VigiaHost *vigia_host_create(VigiaDraw *draw);
bool vigia_host_register(VigiaHost *host, VigiaModule *module);
bool vigia_host_init(VigiaHost *host, const char *explicit_config);
void vigia_host_check_reload(VigiaHost *host);
void vigia_host_update(VigiaHost *host, float delta_time);
size_t vigia_host_collect_input_regions(VigiaHost *host, Rectangle *regions, size_t capacity);
bool vigia_host_needs_redraw(VigiaHost *host);
void vigia_host_render(VigiaHost *host);
void vigia_host_handle_pointer(VigiaHost *host, int x, int y, bool is_down, bool is_released);
void vigia_host_command(VigiaHost *host, VigiaCommand command);
void vigia_host_destroy(VigiaHost *host);

VigiaModule *vigia_menu_module_create(void);
VigiaModule *vigia_osd_module_create(void);
VigiaModule *vigia_notify_module_create(void);

#endif
