#ifndef VIGIA_MODULE_H
#define VIGIA_MODULE_H

#include <stdbool.h>
#include <stddef.h>

#include "vigia_config.h"
#include "vigia_draw.h"
#include "vigia_layout.h"
#include "vigia_model.h"

typedef struct VigiaHost VigiaHost;
typedef struct VigiaModule VigiaModule;

struct VigiaModule {
    const char *name;
    void *state;
    bool (*init)(VigiaModule *self, VigiaHost *host);
    void (*shutdown)(VigiaModule *self);
    bool (*prepare_config)(VigiaModule *self, const VigiaConfig *candidate);
    void (*abort_config)(VigiaModule *self);
    void (*apply_config)(VigiaModule *self, const VigiaConfig *config);
    void (*update)(VigiaModule *self, float delta_time);
    size_t (*get_input_regions)(const VigiaModule *self, VigiaRect *regions, size_t capacity);
    bool (*presentation_changed)(VigiaModule *self);
    void (*render)(VigiaModule *self, VigiaDraw *draw);
    bool (*handle_pointer)(VigiaModule *self, int x, int y, bool is_down, bool is_released);
    void (*handle_command)(VigiaModule *self, VigiaCommand command);
};

#endif
