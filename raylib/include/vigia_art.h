#ifndef VIGIA_ART_H
#define VIGIA_ART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "raylib.h"

#define VIGIA_ART_MAX_CACHE 8U
#define VIGIA_ART_MAX_SIZE (4U * 1024U * 1024U)
#define VIGIA_ART_MAX_DIM 4096

typedef struct VigiaArtLoader VigiaArtLoader;

typedef struct {
    Image image;
    bool ready;
    bool success;
    uint64_t generation;
} VigiaArtResult;

VigiaArtLoader *vigia_art_loader_create(void);
uint64_t vigia_art_loader_request(VigiaArtLoader *loader, const char *url_or_path);
bool vigia_art_loader_poll(VigiaArtLoader *loader, uint64_t request_id, VigiaArtResult *result);
void vigia_art_loader_cancel(VigiaArtLoader *loader, uint64_t request_id);
void vigia_art_loader_destroy(VigiaArtLoader *loader);
void vigia_art_result_free(VigiaArtResult *result);

bool vigia_art_is_valid_url(const char *url_or_path);

#endif
