#ifndef VIGIA_WATCHER_H
#define VIGIA_WATCHER_H

#include <stdbool.h>
#include <stddef.h>

#define VIGIA_WATCHER_MAX_TARGETS 256U

typedef struct VigiaWatcher VigiaWatcher;

VigiaWatcher *vigia_watcher_create(void);
bool vigia_watcher_add_path(VigiaWatcher *watcher, const char *path);
void vigia_watcher_clear_paths(VigiaWatcher *watcher);
bool vigia_watcher_check_modified(VigiaWatcher *watcher);
void vigia_watcher_destroy(VigiaWatcher *watcher);

#endif
