#ifndef VIGIA_IMAGE_H
#define VIGIA_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include "raylib.h"

#define VIGIA_IMAGE_BUDGET (64U * 1024U * 1024U)
#define VIGIA_IMAGE_ENCODED_MAX (4U * 1024U * 1024U)

bool vigia_image_reserve(size_t bytes);
void vigia_image_release(size_t bytes);
void *vigia_image_alloc(size_t bytes);
void *vigia_image_realloc(void *data, size_t bytes);
void vigia_image_free(void *data);
unsigned char *vigia_image_read_file(const char *path, size_t *length);
Image vigia_image_decode(const unsigned char *data, size_t length);
Image vigia_image_copy(Image image);
Image vigia_image_contain(Image image, int size);
void vigia_image_unload(Image image);

#endif
