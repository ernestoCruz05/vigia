#include "vigia_image.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static atomic_size_t image_bytes;

typedef union {
    max_align_t alignment;
    size_t bytes;
} ImageAllocation;

bool vigia_image_reserve(size_t bytes)
{
    size_t used = atomic_load(&image_bytes);
    do {
        if (bytes > VIGIA_IMAGE_BUDGET || used > VIGIA_IMAGE_BUDGET - bytes) {
            return false;
        }
    } while (!atomic_compare_exchange_weak(&image_bytes, &used, used + bytes));
    return true;
}

void vigia_image_release(size_t bytes)
{
    (void)atomic_fetch_sub(&image_bytes, bytes);
}

void *vigia_image_alloc(size_t bytes)
{
    if (bytes > SIZE_MAX - sizeof(ImageAllocation)) {
        return NULL;
    }
    const size_t total = bytes + sizeof(ImageAllocation);
    if (!vigia_image_reserve(total)) {
        return NULL;
    }
    ImageAllocation *allocation = malloc(total);
    if (allocation == NULL) {
        vigia_image_release(total);
        return NULL;
    }
    allocation->bytes = total;
    return allocation + 1;
}

void vigia_image_free(void *data)
{
    if (data != NULL) {
        ImageAllocation *allocation = (ImageAllocation *)data - 1;
        const size_t bytes = allocation->bytes;
        free(allocation);
        vigia_image_release(bytes);
    }
}

void *vigia_image_realloc(void *data, size_t bytes)
{
    if (data == NULL) {
        return vigia_image_alloc(bytes);
    }
    if (bytes > SIZE_MAX - sizeof(ImageAllocation)) {
        return NULL;
    }
    ImageAllocation *allocation = (ImageAllocation *)data - 1;
    const size_t old_bytes = allocation->bytes;
    const size_t total = bytes + sizeof(ImageAllocation);
    if (total > old_bytes && !vigia_image_reserve(total - old_bytes)) {
        return NULL;
    }
    ImageAllocation *replacement = realloc(allocation, total);
    if (replacement == NULL) {
        if (total > old_bytes) {
            vigia_image_release(total - old_bytes);
        }
        return NULL;
    }
    replacement->bytes = total;
    if (old_bytes > total) {
        vigia_image_release(old_bytes - total);
    }
    return replacement + 1;
}

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_NO_GIF
#define STBI_NO_STDIO
#define STBI_MALLOC(size) vigia_image_alloc(size)
#define STBI_REALLOC(data, size) vigia_image_realloc(data, size)
#define STBI_FREE(data) vigia_image_free(data)
#include <external/stb_image.h>

unsigned char *vigia_image_read_file(const char *path, size_t *length)
{
    if (path == NULL || length == NULL) {
        return NULL;
    }
    const int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        return NULL;
    }
    unsigned char *data = NULL;
    struct stat status = {0};
    if (fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) || status.st_size <= 0
        || (uintmax_t)status.st_size > VIGIA_IMAGE_ENCODED_MAX) {
        goto out;
    }
    const size_t size = (size_t)status.st_size;
    data = vigia_image_alloc(size + 1U);
    if (data == NULL) {
        goto out;
    }
    size_t used = 0U;
    while (used <= size) {
        const ssize_t got = read(fd, data + used, size + 1U - used);
        if (got < 0 && errno == EINTR) {
            continue;
        }
        if (got < 0) {
            vigia_image_free(data);
            data = NULL;
            goto out;
        }
        if (got == 0) {
            break;
        }
        used += (size_t)got;
    }
    if (used != size) {
        vigia_image_free(data);
        data = NULL;
    } else {
        data[size] = 0U;
        *length = size;
    }
out:
    if (close(fd) != 0) {
        vigia_image_free(data);
        data = NULL;
    }
    return data;
}

Image vigia_image_decode(const unsigned char *data, size_t length)
{
    Image image = {0};
    if (data == NULL || length == 0U || length > VIGIA_IMAGE_ENCODED_MAX) {
        return image;
    }
    int width = 0;
    int height = 0;
    int channels = 0;
    if (!stbi_info_from_memory(data, (int)length, &width, &height, &channels)
        || width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        return image;
    }
    unsigned char *pixels = stbi_load_from_memory(data, (int)length, &width, &height, &channels, 4);
    if (pixels == NULL) {
        return image;
    }
    image = (Image){ .data = pixels, .width = width, .height = height,
        .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    return image;
}

Image vigia_image_copy(Image image)
{
    if (image.data == NULL || image.width <= 0 || image.height <= 0
        || image.width > 4096 || image.height > 4096
        || image.format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8) {
        return (Image){0};
    }
    const size_t bytes = (size_t)image.width * (size_t)image.height * 4U;
    void *copy = vigia_image_alloc(bytes);
    if (copy == NULL) {
        return (Image){0};
    }
    memcpy(copy, image.data, bytes);
    image.data = copy;
    return image;
}

Image vigia_image_contain(Image image, int size)
{
    if (image.data == NULL || image.width <= 0 || image.height <= 0
        || image.width > 4096 || image.height > 4096 || size <= 0 || size > 4096) {
        return (Image){0};
    }
    const size_t bytes = (size_t)size * (size_t)size * 4U;
    unsigned char *output = vigia_image_alloc(bytes);
    if (output == NULL) {
        return (Image){0};
    }
    memset(output, 0, bytes);
    const double scale = fmin((double)size / image.width, (double)size / image.height);
    const int width = (int)fmax(1.0, round(image.width * scale));
    const int height = (int)fmax(1.0, round(image.height * scale));
    const int ox = (size - width) / 2;
    const int oy = (size - height) / 2;
    const unsigned char *input = image.data;
    for (int y = 0; y < height; y++) {
        const double sy = fmax(0.0, fmin((y + 0.5) * image.height / height - 0.5, image.height - 1.0));
        const int y0 = (int)sy;
        const int y1 = y0 + 1 < image.height ? y0 + 1 : y0;
        for (int x = 0; x < width; x++) {
            const double sx = fmax(0.0, fmin((x + 0.5) * image.width / width - 0.5, image.width - 1.0));
            const int x0 = (int)sx;
            const int x1 = x0 + 1 < image.width ? x0 + 1 : x0;
            const double weights[4] = {
                (1.0 - (sx - x0)) * (1.0 - (sy - y0)),
                (sx - x0) * (1.0 - (sy - y0)),
                (1.0 - (sx - x0)) * (sy - y0), (sx - x0) * (sy - y0) };
            const size_t indices[4] = {
                ((size_t)y0 * (size_t)image.width + (size_t)x0) * 4U,
                ((size_t)y0 * (size_t)image.width + (size_t)x1) * 4U,
                ((size_t)y1 * (size_t)image.width + (size_t)x0) * 4U,
                ((size_t)y1 * (size_t)image.width + (size_t)x1) * 4U };
            double alpha = 0.0;
            double colors[3] = {0};
            for (size_t sample = 0U; sample < 4U; sample++) {
                const double coverage = weights[sample] * input[indices[sample] + 3U];
                alpha += coverage;
                for (size_t c = 0U; c < 3U; c++) {
                    colors[c] += coverage * input[indices[sample] + c];
                }
            }
            const size_t dest = ((size_t)(y + oy) * (size_t)size + (size_t)(x + ox)) * 4U;
            for (size_t c = 0U; c < 3U; c++) {
                output[dest + c] = alpha > 0.0 ? (unsigned char)fmin(255.0, round(colors[c] / alpha)) : 0U;
            }
            output[dest + 3U] = (unsigned char)fmin(255.0, round(alpha));
        }
    }
    return (Image){ .data = output, .width = size, .height = size,
        .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
}

void vigia_image_unload(Image image)
{
    vigia_image_free(image.data);
}
