#include "vigia_watcher.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    int wd;
    char name[256];
    char path[PATH_MAX];
} VigiaWatchTarget;

struct VigiaWatcher {
    int fd;
    size_t count;
    VigiaWatchTarget targets[VIGIA_WATCHER_MAX_TARGETS];
    bool pending;
    struct timespec last_event;
};

static bool bind_target(int fd, VigiaWatchTarget *target)
{
    char parent[PATH_MAX] = {0};
    memcpy(parent, target->path, strlen(target->path) + 1U);
    char *slash = strrchr(parent, '/');
    if (slash == NULL || slash[1] == '\0') {
        return false;
    }
    if (slash == parent) {
        parent[1] = '\0';
    } else {
        *slash = '\0';
    }
    const uint32_t mask = IN_CLOSE_WRITE | IN_MOVED_TO | IN_MOVED_FROM | IN_CREATE
        | IN_DELETE | IN_ATTRIB | IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR;
    for (;;) {
        const int wd = inotify_add_watch(fd, parent, mask);
        if (wd >= 0) {
            const char *component = target->path + strlen(parent);
            while (*component == '/') {
                component++;
            }
            const size_t length = strcspn(component, "/");
            if (length == 0U || length >= sizeof target->name) {
                return false;
            }
            memcpy(target->name, component, length);
            target->name[length] = '\0';
            target->wd = wd;
            return true;
        }
        if (strcmp(parent, "/") == 0) {
            return false;
        }
        slash = strrchr(parent, '/');
        if (slash == NULL) {
            return false;
        }
        if (slash == parent) {
            parent[1] = '\0';
        } else {
            *slash = '\0';
        }
    }
}

VigiaWatcher *vigia_watcher_create(void)
{
    const int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd < 0) {
        return NULL;
    }
    VigiaWatcher *watcher = calloc(1U, sizeof *watcher);
    if (watcher == NULL) {
        (void)close(fd);
        return NULL;
    }
    watcher->fd = fd;
    return watcher;
}

bool vigia_watcher_add_path(VigiaWatcher *watcher, const char *path)
{
    if (watcher == NULL || watcher->fd < 0 || path == NULL || path[0] == '\0') {
        return false;
    }
    VigiaWatchTarget target = { .wd = -1 };
    if (path[0] == '/') {
        if (strlen(path) >= sizeof target.path) {
            return false;
        }
        memcpy(target.path, path, strlen(path) + 1U);
    } else {
        char directory[PATH_MAX] = {0};
        if (getcwd(directory, sizeof directory) == NULL) {
            return false;
        }
        const int length = snprintf(target.path, sizeof target.path, "%s/%s", directory, path);
        if (length < 0 || (size_t)length >= sizeof target.path) {
            return false;
        }
    }
    for (size_t i = 0U; i < watcher->count; i++) {
        if (strcmp(watcher->targets[i].path, target.path) == 0) {
            return true;
        }
    }
    if (watcher->count >= VIGIA_WATCHER_MAX_TARGETS || !bind_target(watcher->fd, &target)) {
        return false;
    }
    watcher->targets[watcher->count++] = target;
    return true;
}

static bool matches(const VigiaWatcher *watcher, const struct inotify_event *event, const char *name)
{
    if ((event->mask & IN_Q_OVERFLOW) != 0U) {
        return true;
    }
    for (size_t i = 0U; i < watcher->count; i++) {
        if (watcher->targets[i].wd == event->wd
            && (name == NULL || strcmp(watcher->targets[i].name, name) == 0)) {
            return true;
        }
    }
    return false;
}

static bool consume_events(VigiaWatcher *watcher)
{
    char buffer[8192];
    bool modified = false;
    for (;;) {
        const ssize_t length = read(watcher->fd, buffer, sizeof buffer);
        if (length < 0 && errno == EINTR) {
            continue;
        }
        if (length <= 0) {
            return modified;
        }
        size_t offset = 0U;
        while ((size_t)length - offset >= sizeof(struct inotify_event)) {
            struct inotify_event event = {0};
            memcpy(&event, buffer + offset, sizeof event);
            offset += sizeof event;
            if ((size_t)event.len > (size_t)length - offset) {
                modified = true;
                break;
            }
            const char *name = event.len > 0U ? buffer + offset : NULL;
            if ((name == NULL || memchr(name, '\0', event.len) != NULL) && matches(watcher, &event, name)) {
                modified = true;
            }
            offset += event.len;
        }
    }
}

static bool rebind(VigiaWatcher *watcher)
{
    const int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    VigiaWatchTarget *targets = calloc(watcher->count > 0U ? watcher->count : 1U, sizeof *targets);
    if (targets == NULL) {
        (void)close(fd);
        return false;
    }
    for (size_t i = 0U; i < watcher->count; i++) {
        targets[i] = watcher->targets[i];
        if (!bind_target(fd, &targets[i])) {
            free(targets);
            (void)close(fd);
            return false;
        }
    }
    if (consume_events(watcher)) {
        watcher->pending = true;
        if (clock_gettime(CLOCK_MONOTONIC, &watcher->last_event) != 0) {
            watcher->last_event = (struct timespec){0};
        }
    }
    (void)close(watcher->fd);
    watcher->fd = fd;
    memcpy(watcher->targets, targets, watcher->count * sizeof *targets);
    free(targets);
    return true;
}

bool vigia_watcher_check_modified(VigiaWatcher *watcher)
{
    if (watcher == NULL || watcher->fd < 0) {
        return false;
    }
    struct timespec now = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return consume_events(watcher);
    }
    if (consume_events(watcher)) {
        watcher->pending = true;
        watcher->last_event = now;
        if (!rebind(watcher)) {
            return false;
        }
    }
    const int64_t elapsed = (int64_t)(now.tv_sec - watcher->last_event.tv_sec) * 1000LL
        + (int64_t)(now.tv_nsec - watcher->last_event.tv_nsec) / 1000000LL;
    if (watcher->pending && elapsed >= 100LL && rebind(watcher)) {
        const int64_t latest = (int64_t)(now.tv_sec - watcher->last_event.tv_sec) * 1000LL
            + (int64_t)(now.tv_nsec - watcher->last_event.tv_nsec) / 1000000LL;
        if (latest >= 100LL) {
            watcher->pending = false;
            return true;
        }
    }
    return false;
}

void vigia_watcher_clear_paths(VigiaWatcher *watcher)
{
    if (watcher != NULL) {
        if (watcher->fd >= 0) {
            (void)close(watcher->fd);
        }
        watcher->fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        watcher->count = 0U;
        watcher->pending = false;
    }
}

void vigia_watcher_destroy(VigiaWatcher *watcher)
{
    if (watcher != NULL) {
        if (watcher->fd >= 0) {
            (void)close(watcher->fd);
        }
        free(watcher);
    }
}
