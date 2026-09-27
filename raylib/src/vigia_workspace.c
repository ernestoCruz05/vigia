#include "vigia_workspace.h"
#include "ext-workspace-v1-client-protocol.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define WORKSPACE_LIMIT 256U
#define GROUP_LIMIT 32U

typedef struct {
    VigiaWorkspace *service;
    struct ext_workspace_group_handle_v1 *handle;
    bool selected;
} WorkspaceGroup;

typedef struct {
    struct ext_workspace_handle_v1 *handle;
    WorkspaceGroup *group;
    int number;
    uint32_t flags;
} Workspace;

typedef struct {
    int ids[VIGIA_WORKSPACE_CAP];
    bool urgent[VIGIA_WORKSPACE_CAP];
    size_t count;
    int active;
} WorkspaceSnapshot;

struct VigiaWorkspace {
    struct wl_display *display;
    struct wl_output *output;
    struct wl_event_queue *queue;
    struct wl_registry *registry;
    struct ext_workspace_manager_v1 *manager;
    uint32_t manager_name;
    WorkspaceGroup groups[GROUP_LIMIT];
    Workspace workspaces[WORKSPACE_LIMIT];
    WorkspaceSnapshot snapshot;
    bool overflow;
    bool closing;
};

static Workspace *find_workspace(VigiaWorkspace *service, struct ext_workspace_handle_v1 *handle)
{
    if (handle != NULL) {
        for (size_t i = 0U; i < WORKSPACE_LIMIT; i++) {
            if (service->workspaces[i].handle == handle) {
                return &service->workspaces[i];
            }
        }
    }
    return NULL;
}

static void workspace_id(void *data, struct ext_workspace_handle_v1 *handle, const char *id)
{
    (void)data;
    (void)handle;
    (void)id;
}

static void workspace_name(void *data, struct ext_workspace_handle_v1 *handle, const char *name)
{
    (void)handle;
    Workspace *workspace = data;
    workspace->number = 0;
    if (name == NULL || name[0] < '1' || name[0] > '9') {
        return;
    }
    errno = 0;
    char *end = NULL;
    const long number = strtol(name, &end, 10);
    if (errno == 0 && end != name && *end == '\0' && number > 0 && number <= INT_MAX) {
        workspace->number = (int)number;
    }
}

static void workspace_coordinates(void *data, struct ext_workspace_handle_v1 *handle,
                                  struct wl_array *coordinates)
{
    (void)data;
    (void)handle;
    (void)coordinates;
}

static void workspace_state(void *data, struct ext_workspace_handle_v1 *handle, uint32_t flags)
{
    (void)handle;
    Workspace *workspace = data;
    workspace->flags = flags;
}

static void workspace_capabilities(void *data, struct ext_workspace_handle_v1 *handle,
                                   uint32_t capabilities)
{
    (void)data;
    (void)handle;
    (void)capabilities;
}

static void workspace_removed(void *data, struct ext_workspace_handle_v1 *handle)
{
    Workspace *workspace = data;
    ext_workspace_handle_v1_destroy(handle);
    *workspace = (Workspace){0};
}

static const struct ext_workspace_handle_v1_listener workspace_listener = {
    .id = workspace_id, .name = workspace_name, .coordinates = workspace_coordinates,
    .state = workspace_state, .capabilities = workspace_capabilities, .removed = workspace_removed,
};

static void group_capabilities(void *data, struct ext_workspace_group_handle_v1 *handle,
                               uint32_t capabilities)
{
    (void)data;
    (void)handle;
    (void)capabilities;
}

static void group_output_enter(void *data, struct ext_workspace_group_handle_v1 *handle,
                               struct wl_output *output)
{
    (void)handle;
    WorkspaceGroup *group = data;
    if (output != NULL && output == group->service->output) {
        group->selected = true;
    }
}

static void group_output_leave(void *data, struct ext_workspace_group_handle_v1 *handle,
                               struct wl_output *output)
{
    (void)handle;
    WorkspaceGroup *group = data;
    if (output == group->service->output) {
        group->selected = false;
    }
}

static void group_workspace_enter(void *data, struct ext_workspace_group_handle_v1 *handle,
                                  struct ext_workspace_handle_v1 *workspace_handle)
{
    (void)handle;
    WorkspaceGroup *group = data;
    Workspace *workspace = find_workspace(group->service, workspace_handle);
    if (workspace != NULL) {
        workspace->group = group;
    }
}

static void group_workspace_leave(void *data, struct ext_workspace_group_handle_v1 *handle,
                                  struct ext_workspace_handle_v1 *workspace_handle)
{
    (void)handle;
    WorkspaceGroup *group = data;
    Workspace *workspace = find_workspace(group->service, workspace_handle);
    if (workspace != NULL && workspace->group == group) {
        workspace->group = NULL;
    }
}

static void group_removed(void *data, struct ext_workspace_group_handle_v1 *handle)
{
    WorkspaceGroup *group = data;
    for (size_t i = 0U; i < WORKSPACE_LIMIT; i++) {
        if (group->service->workspaces[i].group == group) {
            group->service->workspaces[i].group = NULL;
        }
    }
    ext_workspace_group_handle_v1_destroy(handle);
    *group = (WorkspaceGroup){0};
}

static const struct ext_workspace_group_handle_v1_listener group_listener = {
    .capabilities = group_capabilities, .output_enter = group_output_enter,
    .output_leave = group_output_leave, .workspace_enter = group_workspace_enter,
    .workspace_leave = group_workspace_leave, .removed = group_removed,
};

static void manager_group(void *data, struct ext_workspace_manager_v1 *manager,
                          struct ext_workspace_group_handle_v1 *handle)
{
    (void)manager;
    VigiaWorkspace *service = data;
    for (size_t i = 0U; i < GROUP_LIMIT; i++) {
        if (service->groups[i].handle == NULL) {
            service->groups[i] = (WorkspaceGroup){ .service = service, .handle = handle };
            if (ext_workspace_group_handle_v1_add_listener(handle, &group_listener,
                                                           &service->groups[i]) == 0) {
                return;
            }
            service->groups[i] = (WorkspaceGroup){0};
            break;
        }
    }
    service->overflow = true;
    ext_workspace_group_handle_v1_destroy(handle);
}

static void manager_workspace(void *data, struct ext_workspace_manager_v1 *manager,
                              struct ext_workspace_handle_v1 *handle)
{
    (void)manager;
    VigiaWorkspace *service = data;
    for (size_t i = 0U; i < WORKSPACE_LIMIT; i++) {
        if (service->workspaces[i].handle == NULL) {
            service->workspaces[i] = (Workspace){ .handle = handle };
            if (ext_workspace_handle_v1_add_listener(handle, &workspace_listener,
                                                     &service->workspaces[i]) == 0) {
                return;
            }
            service->workspaces[i] = (Workspace){0};
            break;
        }
    }
    service->overflow = true;
    ext_workspace_handle_v1_destroy(handle);
}

static bool visible(const Workspace *workspace)
{
    return workspace->handle != NULL && workspace->group != NULL
        && workspace->group->selected && workspace->number > 0
        && (workspace->flags & EXT_WORKSPACE_HANDLE_V1_STATE_HIDDEN) == 0U;
}

static void manager_done(void *data, struct ext_workspace_manager_v1 *manager)
{
    (void)manager;
    VigiaWorkspace *service = data;
    WorkspaceSnapshot next = {0};
    const Workspace *selected[VIGIA_WORKSPACE_CAP] = {0};
    const Workspace *active = NULL;
    if (!service->overflow) {
        for (size_t i = 0U; i < WORKSPACE_LIMIT; i++) {
            const Workspace *workspace = &service->workspaces[i];
            if (!visible(workspace)) {
                continue;
            }
            if ((workspace->flags & EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE) != 0U) {
                active = workspace;
            }
            size_t position = 0U;
            while (position < next.count && selected[position]->number < workspace->number) {
                position++;
            }
            if (position == VIGIA_WORKSPACE_CAP
                || (position < next.count && selected[position]->number == workspace->number)) {
                continue;
            }
            if (next.count < VIGIA_WORKSPACE_CAP) {
                next.count++;
            }
            for (size_t j = next.count - 1U; j > position; j--) {
                selected[j] = selected[j - 1U];
            }
            selected[position] = workspace;
        }
        if (active != NULL && next.count > 0U) {
            next.active = active->number;
            if (active->number > selected[next.count - 1U]->number) {
                selected[next.count - 1U] = active;
            }
        }
        for (size_t i = 0U; i < next.count; i++) {
            next.ids[i] = selected[i]->number;
            next.urgent[i] = (selected[i]->flags & EXT_WORKSPACE_HANDLE_V1_STATE_URGENT) != 0U;
        }
    }
    service->snapshot = next;
}

static void clear_handles(VigiaWorkspace *service)
{
    for (size_t i = 0U; i < WORKSPACE_LIMIT; i++) {
        if (service->workspaces[i].handle != NULL) {
            workspace_removed(&service->workspaces[i], service->workspaces[i].handle);
        }
    }
    for (size_t i = 0U; i < GROUP_LIMIT; i++) {
        if (service->groups[i].handle != NULL) {
            group_removed(&service->groups[i], service->groups[i].handle);
        }
    }
    service->snapshot = (WorkspaceSnapshot){0};
    service->overflow = false;
}

static void manager_finished(void *data, struct ext_workspace_manager_v1 *manager)
{
    VigiaWorkspace *service = data;
    ext_workspace_manager_v1_destroy(manager);
    service->manager = NULL;
    clear_handles(service);
}

static const struct ext_workspace_manager_v1_listener manager_listener = {
    .workspace_group = manager_group, .workspace = manager_workspace,
    .done = manager_done, .finished = manager_finished,
};

static void registry_global(void *data, struct wl_registry *registry, uint32_t name,
                            const char *interface, uint32_t version)
{
    VigiaWorkspace *service = data;
    if (service->closing || service->manager != NULL || version < 1U
        || strcmp(interface, ext_workspace_manager_v1_interface.name) != 0) {
        return;
    }
    service->manager = wl_registry_bind(registry, name, &ext_workspace_manager_v1_interface, 1U);
    if (service->manager == NULL) {
        return;
    }
    if (ext_workspace_manager_v1_add_listener(service->manager, &manager_listener, service) != 0) {
        ext_workspace_manager_v1_destroy(service->manager);
        service->manager = NULL;
        return;
    }
    service->manager_name = name;
}

static void registry_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    (void)registry;
    VigiaWorkspace *service = data;
    if (name == service->manager_name) {
        if (service->manager != NULL) {
            ext_workspace_manager_v1_stop(service->manager);
            ext_workspace_manager_v1_destroy(service->manager);
            service->manager = NULL;
        }
        service->manager_name = 0U;
        clear_handles(service);
    }
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global, .global_remove = registry_remove,
};

VigiaWorkspace *vigia_workspace_create(struct wl_display *display, struct wl_output *output)
{
    if (display == NULL || output == NULL) {
        return NULL;
    }
    VigiaWorkspace *service = calloc(1U, sizeof *service);
    if (service == NULL) {
        return NULL;
    }
    service->display = display;
    service->output = output;
    service->queue = wl_display_create_queue(display);
    if (service->queue == NULL) {
        goto fail;
    }
    service->registry = wl_display_get_registry(display);
    if (service->registry == NULL) {
        goto fail;
    }
    wl_proxy_set_queue((struct wl_proxy *)service->registry, service->queue);
    if (wl_registry_add_listener(service->registry, &registry_listener, service) != 0) {
        goto fail;
    }
    return service;
fail:
    vigia_workspace_destroy(service);
    return NULL;
}

void vigia_workspace_update(VigiaWorkspace *service, VigiaState *state)
{
    if (state == NULL) {
        return;
    }
    WorkspaceSnapshot snapshot = {0};
    if (service != NULL && wl_display_dispatch_queue_pending(service->display, service->queue) >= 0
        && wl_display_get_error(service->display) == 0) {
        snapshot = service->snapshot;
    }
    memcpy(state->workspace_ids, snapshot.ids, sizeof snapshot.ids);
    memcpy(state->workspace_urgent, snapshot.urgent, sizeof snapshot.urgent);
    state->workspace_count = snapshot.count;
    state->active_workspace = snapshot.active;
    vigia_state_update_tags(state, 0.0F);
}

void vigia_workspace_destroy(VigiaWorkspace *service)
{
    if (service == NULL) {
        return;
    }
    service->closing = true;
    if (service->manager != NULL) {
        ext_workspace_manager_v1_stop(service->manager);
        if (wl_display_roundtrip_queue(service->display, service->queue) < 0) {
            service->snapshot = (WorkspaceSnapshot){0};
        }
        if (service->manager != NULL) {
            ext_workspace_manager_v1_destroy(service->manager);
        }
    }
    clear_handles(service);
    if (service->registry != NULL) {
        wl_registry_destroy(service->registry);
    }
    if (service->queue != NULL) {
        wl_event_queue_destroy(service->queue);
    }
    free(service);
}
