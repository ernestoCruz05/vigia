#ifndef VIGIA_WAYLAND_H
#define VIGIA_WAYLAND_H

#include <stdbool.h>

struct wl_display;
struct wl_seat;

typedef struct {
    int x;
    int y;
    bool present;
    bool down;
    bool released;
} VigiaPointerEvent;

void vigia_wayland_init_hints(void);
bool vigia_wayland_prepare(void);
struct wl_display *vigia_wayland_get_display(void);
struct wl_seat *vigia_wayland_get_seat(void);
struct wl_output *vigia_wayland_get_output(void);
bool vigia_wayland_poll_pointer(VigiaPointerEvent *event);

#endif
