#ifndef VIGIA_WORKSPACE_H
#define VIGIA_WORKSPACE_H

#include "vigia_model.h"

struct wl_display;
struct wl_output;
typedef struct VigiaWorkspace VigiaWorkspace;

VigiaWorkspace *vigia_workspace_create(struct wl_display *display, struct wl_output *output);
void vigia_workspace_update(VigiaWorkspace *service, VigiaState *state);
void vigia_workspace_destroy(VigiaWorkspace *service);

#endif
