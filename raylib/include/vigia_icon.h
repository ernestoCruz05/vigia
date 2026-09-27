#ifndef VIGIA_ICON_H
#define VIGIA_ICON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "raylib.h"

#define VIGIA_ICON_MAX_ENCODED_BYTES ((size_t)4U * 1024U * 1024U)
#define VIGIA_ICON_MAX_DIMENSION 4096
#define VIGIA_ICON_DEFAULT_MAX_BYTES ((size_t)64U * 1024U * 1024U)

typedef struct VigiaIconCache VigiaIconCache;

bool vigia_icon_load_image(const char *path, int target_size, Image *out_image);
void vigia_icon_free_image(Image *image);

VigiaIconCache *vigia_icon_cache_create(size_t max_memory_bytes);
Texture2D vigia_icon_cache_get(VigiaIconCache *cache, const char *path, int target_size);
Texture2D vigia_icon_cache_find(VigiaIconCache *cache, const char *path, int target_size);
void vigia_icon_cache_invalidate(VigiaIconCache *cache, const char *path);
void vigia_icon_cache_destroy(VigiaIconCache *cache);

void vigia_icon_cleanup(void);

#endif
