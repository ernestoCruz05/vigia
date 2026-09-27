#define _GNU_SOURCE
#include "vigia_window.h"

#include <errno.h>
#include <spawn.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static pid_t owned_children[256];

void vigia_window_reap_children(void)
{
    for (size_t i = 0U; i < sizeof owned_children / sizeof owned_children[0]; i++) {
        if (owned_children[i] > 0) {
            const pid_t result = waitpid(owned_children[i], NULL, WNOHANG);
            if (result > 0 || (result < 0 && errno == ECHILD)) {
                owned_children[i] = 0;
            }
        }
    }
}

static void toplevel_handle_title(void *data,
                                  struct zwlr_foreign_toplevel_handle_v1 *toplevel,
                                  const char *title)
{
    (void)toplevel;
    VigiaTrackedWindow *win = data;
    if (win == NULL) {
        return;
    }
    (void)snprintf(win->title, sizeof win->title, "%s", title != NULL ? title : "");
}

static void toplevel_handle_app_id(void *data,
                                   struct zwlr_foreign_toplevel_handle_v1 *toplevel,
                                   const char *app_id)
{
    (void)toplevel;
    VigiaTrackedWindow *win = data;
    if (win == NULL) {
        return;
    }
    (void)snprintf(win->app_id, sizeof win->app_id, "%s", app_id != NULL ? app_id : "");
}

static void toplevel_handle_output_enter(void *data,
                                        struct zwlr_foreign_toplevel_handle_v1 *toplevel,
                                        struct wl_output *output)
{
    (void)data;
    (void)toplevel;
    (void)output;
}

static void toplevel_handle_output_leave(void *data,
                                        struct zwlr_foreign_toplevel_handle_v1 *toplevel,
                                        struct wl_output *output)
{
    (void)data;
    (void)toplevel;
    (void)output;
}

static void toplevel_handle_state(void *data,
                                  struct zwlr_foreign_toplevel_handle_v1 *toplevel,
                                  struct wl_array *state)
{
    (void)toplevel;
    VigiaTrackedWindow *win = data;
    if (win == NULL || state == NULL) {
        return;
    }
    bool activated = false;
    const uint32_t *entry = state->data;
    const size_t count = state->size / sizeof(uint32_t);
    for (size_t i = 0U; i < count; i++) {
        if (entry[i] == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED) {
            activated = true;
            break;
        }
    }
    win->is_active = activated;
    if (activated) {
        static uint64_t global_clock = 1000U;
        win->last_active_time = ++global_clock;
    }
}

static void toplevel_handle_done(void *data,
                                 struct zwlr_foreign_toplevel_handle_v1 *toplevel)
{
    (void)data;
    (void)toplevel;
}

static void toplevel_handle_closed(void *data,
                                   struct zwlr_foreign_toplevel_handle_v1 *toplevel)
{
    VigiaTrackedWindow *win = data;
    if (win != NULL) {
        win->closed = true;
        win->handle = NULL;
    }
    zwlr_foreign_toplevel_handle_v1_destroy(toplevel);
}

static void toplevel_handle_parent(void *data,
                                   struct zwlr_foreign_toplevel_handle_v1 *toplevel,
                                   struct zwlr_foreign_toplevel_handle_v1 *parent)
{
    (void)data;
    (void)toplevel;
    (void)parent;
}

static const struct zwlr_foreign_toplevel_handle_v1_listener handle_listener = {
    .title = toplevel_handle_title,
    .app_id = toplevel_handle_app_id,
    .output_enter = toplevel_handle_output_enter,
    .output_leave = toplevel_handle_output_leave,
    .state = toplevel_handle_state,
    .done = toplevel_handle_done,
    .closed = toplevel_handle_closed,
    .parent = toplevel_handle_parent,
};

static void manager_handle_toplevel(void *data,
                                    struct zwlr_foreign_toplevel_manager_v1 *manager,
                                    struct zwlr_foreign_toplevel_handle_v1 *toplevel)
{
    (void)manager;
    VigiaWindowService *service = data;
    if (service == NULL || toplevel == NULL) {
        return;
    }
    for (size_t i = 0U; i < VIGIA_WINDOW_MAX; i++) {
        if (service->windows[i].closed || service->windows[i].handle == NULL) {
            service->windows[i] = (VigiaTrackedWindow){
                .handle = toplevel,
                .app_id = {0},
                .title = {0},
                .discovery_order = ++service->discovery_counter,
                .last_active_time = 0U,
                .is_active = false,
                .closed = false,
            };
            service->window_count++;
            zwlr_foreign_toplevel_handle_v1_add_listener(toplevel, &handle_listener, &service->windows[i]);
            return;
        }
    }
}

static void manager_handle_finished(void *data,
                                    struct zwlr_foreign_toplevel_manager_v1 *manager)
{
    (void)manager;
    VigiaWindowService *service = data;
    if (service != NULL) {
        service->ready = false;
        service->finished = true;
    }
}

static const struct zwlr_foreign_toplevel_manager_v1_listener manager_listener = {
    .toplevel = manager_handle_toplevel,
    .finished = manager_handle_finished,
};

static void registry_handle_global(void *data,
                                   struct wl_registry *registry,
                                   uint32_t name,
                                   const char *interface,
                                   uint32_t version)
{
    VigiaWindowService *service = data;
    if (service == NULL || interface == NULL) {
        return;
    }
    if (strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0) {
        service->manager_id = name;
        const uint32_t ver = version < 3U ? version : 3U;
        service->manager = wl_registry_bind(registry, name, &zwlr_foreign_toplevel_manager_v1_interface, ver);
        if (service->manager != NULL) {
            zwlr_foreign_toplevel_manager_v1_add_listener(service->manager, &manager_listener, service);
        }
    }
}

static void registry_handle_global_remove(void *data,
                                          struct wl_registry *registry,
                                          uint32_t name)
{
    (void)registry;
    VigiaWindowService *service = data;
    if (service != NULL && name == service->manager_id) {
        service->finished = true;
        service->ready = false;
    }
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

VigiaWindowService *vigia_window_service_create(struct wl_display *display)
{
    VigiaWindowService *service = calloc(1, sizeof *service);
    if (service == NULL) {
        return NULL;
    }
    service->display = display;
    if (display == NULL) {
        service->ready = false;
        return service;
    }
    service->registry = wl_display_get_registry(display);
    if (service->registry == NULL) {
        service->ready = false;
        return service;
    }
    if (wl_registry_add_listener(service->registry, &registry_listener, service) != 0
        || wl_display_roundtrip(display) < 0) {
        service->finished = true;
        return service;
    }
    if (service->manager != NULL && !service->finished) {
        const int result = wl_display_roundtrip(display);
        service->ready = result >= 0 && !service->finished
            && wl_display_get_error(display) == 0;
    }
    return service;
}

bool vigia_window_service_is_ready(const VigiaWindowService *service)
{
    if (service == NULL) {
        return false;
    }
    return service->ready && !service->finished
        && (service->display == NULL || wl_display_get_error(service->display) == 0);
}

bool vigia_window_service_add_mock_window(VigiaWindowService *service,
                                          const char *app_id, const char *title,
                                          uint64_t active_time)
{
    if (service == NULL || app_id == NULL) {
        return false;
    }
    for (size_t i = 0U; i < VIGIA_WINDOW_MAX; i++) {
        if (!service->windows[i].closed && service->windows[i].app_id[0] == '\0') {
            service->windows[i] = (VigiaTrackedWindow){
                .handle = NULL,
                .app_id = {0},
                .title = {0},
                .discovery_order = ++service->discovery_counter,
                .last_active_time = active_time,
                .is_active = active_time > 0U,
                .closed = false,
            };
            (void)snprintf(service->windows[i].app_id, sizeof service->windows[i].app_id, "%s", app_id);
            (void)snprintf(service->windows[i].title, sizeof service->windows[i].title, "%s", title != NULL ? title : "");
            service->window_count++;
            return true;
        }
    }
    return false;
}

bool vigia_window_matches_app_id(const char *window_app_id,
                                 const char *const *app_ids, size_t app_id_count)
{
    if (window_app_id == NULL || window_app_id[0] == '\0' || app_ids == NULL || app_id_count == 0U) {
        return false;
    }
    for (size_t i = 0U; i < app_id_count; i++) {
        if (app_ids[i] != NULL && strcmp(window_app_id, app_ids[i]) == 0) {
            return true;
        }
    }
    return false;
}

const VigiaTrackedWindow *vigia_window_find_best_match(const VigiaWindowService *service,
                                                     const char *const *app_ids,
                                                     size_t app_id_count)
{
    if (service == NULL || app_ids == NULL || app_id_count == 0U) {
        return NULL;
    }
    const VigiaTrackedWindow *best = NULL;
    for (size_t i = 0U; i < VIGIA_WINDOW_MAX; i++) {
        const VigiaTrackedWindow *win = &service->windows[i];
        if (win->closed || win->app_id[0] == '\0') {
            continue;
        }
        if (vigia_window_matches_app_id(win->app_id, app_ids, app_id_count)) {
            if (best == NULL) {
                best = win;
            } else if (win->last_active_time > best->last_active_time) {
                best = win;
            } else if (win->last_active_time == best->last_active_time) {
                if (win->discovery_order < best->discovery_order) {
                    best = win;
                }
            }
        }
    }
    return best;
}

bool vigia_window_focus(VigiaWindowService *service, const char *const *app_ids,
                        size_t app_id_count, struct wl_seat *seat)
{
    if (!vigia_window_service_is_ready(service) || seat == NULL) {
        return false;
    }
    const VigiaTrackedWindow *best = vigia_window_find_best_match(service, app_ids, app_id_count);
    if (best == NULL || best->handle == NULL) {
        return false;
    }
    zwlr_foreign_toplevel_handle_v1_activate(best->handle, seat);
    return true;
}

bool vigia_window_is_pending_spawn(const VigiaWindowService *service,
                                  const char *entry_id, uint64_t config_gen)
{
    if (service == NULL || entry_id == NULL) {
        return false;
    }
    for (size_t i = 0U; i < VIGIA_WINDOW_PENDING_MAX; i++) {
        if (service->pending[i].active &&
            service->pending[i].config_generation == config_gen &&
            strcmp(service->pending[i].id, entry_id) == 0) {
            return true;
        }
    }
    return false;
}

bool vigia_window_mark_pending_spawn(VigiaWindowService *service,
                                    const char *entry_id, uint64_t config_gen,
                                    const char *const *app_ids, size_t app_id_count)
{
    if (service == NULL || entry_id == NULL || entry_id[0] == '\0'
        || strlen(entry_id) >= sizeof service->pending[0].id || app_id_count > 16U
        || (app_id_count > 0U && app_ids == NULL)
        || vigia_window_is_pending_spawn(service, entry_id, config_gen)) {
        return false;
    }
    for (size_t i = 0U; i < app_id_count; i++) {
        if (app_ids[i] == NULL || app_ids[i][0] == '\0' || strlen(app_ids[i]) >= 128U) {
            return false;
        }
    }
    for (size_t i = 0U; i < VIGIA_WINDOW_PENDING_MAX; i++) {
        VigiaPendingSpawn *pending = &service->pending[i];
        if (!pending->active) {
            *pending = (VigiaPendingSpawn){ .config_generation = config_gen,
                .remaining_seconds = VIGIA_WINDOW_SPAWN_GUARD_SECONDS, .app_id_count = app_id_count, .active = true };
            memcpy(pending->id, entry_id, strlen(entry_id) + 1U);
            for (size_t j = 0U; j < app_id_count; j++) {
                memcpy(pending->app_ids[j], app_ids[j], strlen(app_ids[j]) + 1U);
            }
            return true;
        }
    }
    return false;
}

void vigia_window_clear_pending_spawn(VigiaWindowService *service,
                                     const char *entry_id)
{
    if (service == NULL || entry_id == NULL) {
        return;
    }
    for (size_t i = 0U; i < VIGIA_WINDOW_PENDING_MAX; i++) {
        if (service->pending[i].active && strcmp(service->pending[i].id, entry_id) == 0) {
            service->pending[i].active = false;
        }
    }
}

void vigia_window_update(VigiaWindowService *service, float delta_seconds)
{
    vigia_window_reap_children();
    if (service == NULL) {
        return;
    }
    for (size_t i = 0U; i < VIGIA_WINDOW_PENDING_MAX; i++) {
        if (service->pending[i].active) {
            const char *aliases[16] = {0};
            for (size_t j = 0U; j < service->pending[i].app_id_count; j++) {
                aliases[j] = service->pending[i].app_ids[j];
            }
            if (vigia_window_find_best_match(service, aliases, service->pending[i].app_id_count) != NULL) {
                service->pending[i].active = false;
                continue;
            }
            service->pending[i].remaining_seconds -= delta_seconds;
            if (service->pending[i].remaining_seconds <= 0.0f) {
                service->pending[i].active = false;
            }
        }
    }
}

bool vigia_window_spawn(const char *const *argv, size_t argc)
{
    if (argv == NULL || argc == 0U || argv[0] == NULL || argv[0][0] == '\0') {
        return false;
    }
    if (argc > (SIZE_MAX / sizeof(char *)) - 1U) {
        return false;
    }
    vigia_window_reap_children();
    size_t slot = 0U;
    while (slot < sizeof owned_children / sizeof owned_children[0] && owned_children[slot] != 0) {
        slot++;
    }
    if (slot == sizeof owned_children / sizeof owned_children[0]) {
        return false;
    }
    char **child_argv = calloc(argc + 1U, sizeof *child_argv);
    if (child_argv == NULL) {
        return false;
    }
    for (size_t i = 0U; i < argc; i++) {
        if (argv[i] == NULL) {
            free(child_argv);
            return false;
        }
        child_argv[i] = (char *)(uintptr_t)argv[i];
    }
    const char *home = getenv("HOME");
    posix_spawn_file_actions_t actions;
    if (home == NULL || home[0] != '/' || posix_spawn_file_actions_init(&actions) != 0) {
        free(child_argv);
        return false;
    }
    int error = posix_spawn_file_actions_addchdir_np(&actions, home);
    pid_t pid = 0;
    if (error == 0) {
        error = posix_spawnp(&pid, child_argv[0], &actions, NULL, child_argv, environ);
    }
    const int cleanup_error = posix_spawn_file_actions_destroy(&actions);
    free(child_argv);
    if (error != 0) {
        return false;
    }
    owned_children[slot] = pid;
    if (cleanup_error != 0) {
        (void)fprintf(stderr, "vigia-ray: spawn action cleanup failed: %d\n", cleanup_error);
    }
    return true;
}

void vigia_window_service_destroy(VigiaWindowService *service)
{
    if (service == NULL) {
        return;
    }
    for (size_t i = 0U; i < VIGIA_WINDOW_MAX; i++) {
        if (!service->windows[i].closed && service->windows[i].handle != NULL) {
            zwlr_foreign_toplevel_handle_v1_destroy(service->windows[i].handle);
            service->windows[i].handle = NULL;
        }
    }
    if (service->manager != NULL) {
        zwlr_foreign_toplevel_manager_v1_destroy(service->manager);
        service->manager = NULL;
    }
    if (service->registry != NULL) {
        wl_registry_destroy(service->registry);
        service->registry = NULL;
    }
    free(service);
}
