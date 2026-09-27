#ifndef VIGIA_WINDOW_H
#define VIGIA_WINDOW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <wayland-client.h>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#define VIGIA_WINDOW_MAX 128U
#define VIGIA_WINDOW_PENDING_MAX 64U
#define VIGIA_WINDOW_SPAWN_GUARD_SECONDS 5.0f

typedef struct VigiaTrackedWindow {
    struct zwlr_foreign_toplevel_handle_v1 *handle;
    char app_id[256];
    char title[512];
    uint64_t discovery_order;
    uint64_t last_active_time;
    bool is_active;
    bool closed;
} VigiaTrackedWindow;

typedef struct VigiaPendingSpawn {
    char id[64];
    uint64_t config_generation;
    float remaining_seconds;
    size_t app_id_count;
    char app_ids[16][128];
    bool active;
} VigiaPendingSpawn;

typedef struct VigiaWindowService VigiaWindowService;

struct VigiaWindowService {
    struct wl_display *display;
    struct wl_registry *registry;
    struct zwlr_foreign_toplevel_manager_v1 *manager;
    uint32_t manager_id;
    bool ready;
    bool finished;
    uint64_t discovery_counter;
    uint64_t active_counter;
    size_t window_count;
    VigiaTrackedWindow windows[VIGIA_WINDOW_MAX];
    VigiaPendingSpawn pending[VIGIA_WINDOW_PENDING_MAX];
};

VigiaWindowService *vigia_window_service_create(struct wl_display *display);
bool vigia_window_service_is_ready(const VigiaWindowService *service);

bool vigia_window_service_add_mock_window(VigiaWindowService *service,
                                          const char *app_id, const char *title,
                                          uint64_t active_time);

bool vigia_window_matches_app_id(const char *window_app_id,
                                 const char *const *app_ids, size_t app_id_count);

const VigiaTrackedWindow *vigia_window_find_best_match(const VigiaWindowService *service,
                                                     const char *const *app_ids,
                                                     size_t app_id_count);

bool vigia_window_focus(VigiaWindowService *service, const char *const *app_ids,
                        size_t app_id_count, struct wl_seat *seat);

bool vigia_window_is_pending_spawn(const VigiaWindowService *service,
                                  const char *entry_id, uint64_t config_gen);

bool vigia_window_mark_pending_spawn(VigiaWindowService *service,
                                    const char *entry_id, uint64_t config_gen,
                                    const char *const *app_ids, size_t app_id_count);

void vigia_window_clear_pending_spawn(VigiaWindowService *service,
                                     const char *entry_id);

void vigia_window_update(VigiaWindowService *service, float delta_seconds);
void vigia_window_reap_children(void);

bool vigia_window_spawn(const char *const *argv, size_t argc);

void vigia_window_service_destroy(VigiaWindowService *service);

#endif
