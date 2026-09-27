#include "vigia_icon.h"
#include "vigia_image.h"

#include <cairo.h>
#include <fontconfig/fontconfig.h>
#include <librsvg/rsvg.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define VIGIA_ICON_CACHE_CAPACITY 64U

static size_t cache_instances;

typedef struct {
    char path[4096];
    int size;
    Texture2D texture;
    size_t bytes;
    uint64_t last_used;
} VigiaIconCacheEntry;

struct VigiaIconCache {
    size_t max_bytes;
    size_t current_bytes;
    uint64_t counter;
    VigiaIconCacheEntry entries[VIGIA_ICON_CACHE_CAPACITY];
};

typedef struct {
    size_t depth;
    size_t nodes;
    size_t style_depth;
    char *style;
    size_t style_length;
} SvgValidation;

static bool safe_css(const char *text)
{
    if (strchr(text, '\\') != NULL || strchr(text, '@') != NULL) {
        return false;
    }
    for (const char *p = text; *p != '\0'; p++) {
        if (strncasecmp(p, "url(", 4U) != 0) {
            continue;
        }
        const char *value = p + 4;
        while (g_ascii_isspace(*value)) {
            value++;
        }
        if (*value == '\'' || *value == '"') {
            value++;
        }
        if (*value != '#') {
            return false;
        }
    }
    return true;
}

static void svg_error(GError **error)
{
    g_set_error_literal(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT, "unsafe or oversized SVG");
}

static void svg_start(GMarkupParseContext *context, const gchar *name,
                      const gchar **attributes, const gchar **values, gpointer data, GError **error)
{
    (void)context;
    SvgValidation *validation = data;
    validation->depth++;
    validation->nodes++;
    const char *local = strrchr(name, ':');
    local = local != NULL ? local + 1 : name;
    if (validation->depth > 64U || validation->nodes > 8192U
        || strcasecmp(local, "script") == 0 || strcasecmp(local, "foreignObject") == 0) {
        svg_error(error);
        return;
    }
    if (strcmp(local, "style") == 0) {
        validation->style_depth = validation->depth;
    }
    for (size_t i = 0U; attributes[i] != NULL; i++) {
        const char *attribute = strrchr(attributes[i], ':');
        attribute = attribute != NULL ? attribute + 1 : attributes[i];
        if ((strcasecmp(attribute, "href") == 0 && values[i][0] != '#')
            || strncasecmp(attribute, "on", 2U) == 0 || !safe_css(values[i])) {
            svg_error(error);
            return;
        }
    }
}

static void svg_text(GMarkupParseContext *context, const gchar *text, gsize length, gpointer data, GError **error)
{
    (void)context;
    SvgValidation *validation = data;
    if (validation->style_depth == 0U) {
        return;
    }
    if (length > VIGIA_IMAGE_ENCODED_MAX - validation->style_length) {
        svg_error(error);
        return;
    }
    char *replacement = vigia_image_realloc(validation->style, validation->style_length + length + 1U);
    if (replacement == NULL) {
        svg_error(error);
        return;
    }
    validation->style = replacement;
    memcpy(replacement + validation->style_length, text, length);
    validation->style_length += length;
    replacement[validation->style_length] = '\0';
}

static void svg_end(GMarkupParseContext *context, const gchar *name, gpointer data, GError **error)
{
    (void)context;
    (void)name;
    SvgValidation *validation = data;
    if (validation->depth == validation->style_depth) {
        if (validation->style != NULL && !safe_css(validation->style)) {
            svg_error(error);
        }
        validation->style_depth = 0U;
        validation->style_length = 0U;
    }
    validation->depth--;
}

static void svg_passthrough(GMarkupParseContext *context, const gchar *text, gsize length,
                            gpointer data, GError **error)
{
    (void)context;
    (void)data;
    if ((length >= 9U && strncmp(text, "<!DOCTYPE", 9U) == 0)
        || (length >= 2U && strncmp(text, "<?", 2U) == 0
            && (length < 6U || strncmp(text, "<?xml ", 6U) != 0))) {
        svg_error(error);
    }
}

static bool safe_svg(const unsigned char *bytes, size_t length)
{
    const GMarkupParser parser = {
        .start_element = svg_start, .end_element = svg_end, .text = svg_text, .passthrough = svg_passthrough,
    };
    SvgValidation validation = {0};
    GMarkupParseContext *context = g_markup_parse_context_new(&parser, G_MARKUP_TREAT_CDATA_AS_TEXT, &validation, NULL);
    if (context == NULL) {
        return false;
    }
    GError *error = NULL;
    const bool valid = g_markup_parse_context_parse(context, (const char *)bytes, (gssize)length, &error)
        && g_markup_parse_context_end_parse(context, &error);
    if (error != NULL) {
        g_error_free(error);
    }
    g_markup_parse_context_free(context);
    vigia_image_free(validation.style);
    return valid;
}

static bool load_svg(const unsigned char *bytes, size_t length, int size, Image *image)
{
    if (!safe_svg(bytes, length)) {
        return false;
    }
    GError *error = NULL;
    RsvgHandle *handle = rsvg_handle_new_from_data(bytes, length, &error);
    cairo_surface_t *surface = NULL;
    cairo_t *context = NULL;
    unsigned char *pixels = NULL;
    bool success = false;
    if (handle == NULL) {
        goto done;
    }
    double width = 0.0;
    double height = 0.0;
    if (rsvg_handle_get_intrinsic_size_in_pixels(handle, &width, &height)
        && (!isfinite(width) || !isfinite(height) || width <= 0.0 || height <= 0.0
            || width > 4096.0 || height > 4096.0)) {
        goto done;
    }
    const int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, size);
    if (stride != size * 4) {
        goto done;
    }
    const size_t count = (size_t)size * (size_t)size;
    pixels = vigia_image_alloc(count * 4U);
    if (pixels == NULL) {
        goto done;
    }
    memset(pixels, 0, count * 4U);
    surface = cairo_image_surface_create_for_data(pixels, CAIRO_FORMAT_ARGB32, size, size, stride);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        goto done;
    }
    context = cairo_create(surface);
    const RsvgRectangle viewport = { .width = (double)size, .height = (double)size };
    if (cairo_status(context) != CAIRO_STATUS_SUCCESS
        || !rsvg_handle_render_document(handle, context, &viewport, &error)
        || cairo_status(context) != CAIRO_STATUS_SUCCESS
        || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        goto done;
    }
    cairo_surface_flush(surface);
    for (size_t i = 0U; i < count; i++) {
        uint32_t pixel = 0U;
        memcpy(&pixel, pixels + i * 4U, sizeof pixel);
        const unsigned int alpha = pixel >> 24U;
        const unsigned int channels[3] = { (pixel >> 16U) & 255U, (pixel >> 8U) & 255U, pixel & 255U };
        for (size_t j = 0U; j < 3U; j++) {
            const unsigned int channel = alpha > 0U ? (channels[j] * 255U + alpha / 2U) / alpha : 0U;
            pixels[i * 4U + j] = (unsigned char)(channel > 255U ? 255U : channel);
        }
        pixels[i * 4U + 3U] = (unsigned char)alpha;
    }
    success = true;
done:
    if (context != NULL) {
        cairo_destroy(context);
    }
    if (surface != NULL) {
        cairo_surface_destroy(surface);
    }
    if (handle != NULL) {
        g_object_unref(handle);
    }
    if (error != NULL) {
        g_error_free(error);
    }
    if (success) {
        *image = (Image){ .data = pixels, .width = size, .height = size, .mipmaps = 1,
            .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    } else {
        vigia_image_free(pixels);
    }
    return success;
}

bool vigia_icon_load_image(const char *path, int size, Image *image)
{
    if (path == NULL || image == NULL || size <= 0 || size > 4096) {
        return false;
    }
    *image = (Image){0};
    const char *extension = strrchr(path, '.');
    if (extension == NULL || (strcasecmp(extension, ".svg") != 0 && strcasecmp(extension, ".png") != 0)) {
        return false;
    }
    size_t length = 0U;
    unsigned char *bytes = vigia_image_read_file(path, &length);
    if (bytes == NULL) {
        return false;
    }
    bool success = false;
    if (strcasecmp(extension, ".svg") == 0) {
        success = load_svg(bytes, length, size, image);
    } else {
        static const unsigned char signature[8] = {137U, 80U, 78U, 71U, 13U, 10U, 26U, 10U};
        if (length >= sizeof signature && memcmp(bytes, signature, sizeof signature) == 0) {
            Image decoded = vigia_image_decode(bytes, length);
            if (decoded.data != NULL) {
                *image = vigia_image_contain(decoded, size);
                vigia_image_unload(decoded);
                success = image->data != NULL;
            }
        }
    }
    vigia_image_free(bytes);
    return success;
}

void vigia_icon_free_image(Image *image)
{
    if (image != NULL) {
        vigia_image_unload(*image);
        *image = (Image){0};
    }
}

VigiaIconCache *vigia_icon_cache_create(size_t maximum)
{
    VigiaIconCache *cache = calloc(1U, sizeof *cache);
    if (cache != NULL) {
        cache_instances++;
        cache->max_bytes = maximum > 0U && maximum < VIGIA_IMAGE_BUDGET ? maximum : VIGIA_IMAGE_BUDGET;
    }
    return cache;
}

static void remove_entry(VigiaIconCache *cache, VigiaIconCacheEntry *entry)
{
    if (entry->texture.id != 0U) {
        if (IsWindowReady()) {
            UnloadTexture(entry->texture);
        }
        vigia_image_release(entry->bytes);
        cache->current_bytes -= entry->bytes;
        *entry = (VigiaIconCacheEntry){0};
    }
}

Texture2D vigia_icon_cache_find(VigiaIconCache *cache, const char *path, int size)
{
    if (cache != NULL && path != NULL) {
        for (size_t i = 0U; i < VIGIA_ICON_CACHE_CAPACITY; i++) {
            VigiaIconCacheEntry *entry = &cache->entries[i];
            if (entry->texture.id != 0U && entry->size == size && strcmp(entry->path, path) == 0) {
                entry->last_used = ++cache->counter;
                return entry->texture;
            }
        }
    }
    return (Texture2D){0};
}

Texture2D vigia_icon_cache_get(VigiaIconCache *cache, const char *path, int size)
{
    if (cache == NULL || path == NULL || strlen(path) >= sizeof cache->entries[0].path || size <= 0 || size > 4096) {
        return (Texture2D){0};
    }
    Texture2D texture = vigia_icon_cache_find(cache, path, size);
    if (texture.id != 0U) {
        return texture;
    }
    const size_t bytes = (size_t)size * (size_t)size * 4U;
    if (bytes > cache->max_bytes) {
        return texture;
    }
    size_t slot = 0U;
    for (;;) {
        slot = VIGIA_ICON_CACHE_CAPACITY;
        size_t oldest = VIGIA_ICON_CACHE_CAPACITY;
        for (size_t i = 0U; i < VIGIA_ICON_CACHE_CAPACITY; i++) {
            if (cache->entries[i].texture.id == 0U) {
                slot = i;
            } else if (oldest == VIGIA_ICON_CACHE_CAPACITY
                       || cache->entries[i].last_used < cache->entries[oldest].last_used) {
                oldest = i;
            }
        }
        if (slot < VIGIA_ICON_CACHE_CAPACITY && bytes <= cache->max_bytes - cache->current_bytes) {
            break;
        }
        if (oldest == VIGIA_ICON_CACHE_CAPACITY) {
            return texture;
        }
        remove_entry(cache, &cache->entries[oldest]);
    }
    Image image = {0};
    if (!vigia_icon_load_image(path, size, &image)) {
        return texture;
    }
    if (!vigia_image_reserve(bytes)) {
        vigia_icon_free_image(&image);
        return texture;
    }
    if (IsWindowReady()) {
        texture = LoadTextureFromImage(image);
    } else {
        static unsigned int mock_id = 100U;
        texture = (Texture2D){ .id = ++mock_id, .width = size, .height = size, .mipmaps = 1, .format = image.format };
    }
    vigia_icon_free_image(&image);
    if (texture.id == 0U) {
        vigia_image_release(bytes);
        return texture;
    }
    VigiaIconCacheEntry *entry = &cache->entries[slot];
    memcpy(entry->path, path, strlen(path) + 1U);
    entry->size = size;
    entry->texture = texture;
    entry->bytes = bytes;
    entry->last_used = ++cache->counter;
    cache->current_bytes += bytes;
    return texture;
}

void vigia_icon_cache_invalidate(VigiaIconCache *cache, const char *path)
{
    if (cache != NULL && path != NULL) {
        for (size_t i = 0U; i < VIGIA_ICON_CACHE_CAPACITY; i++) {
            if (strcmp(cache->entries[i].path, path) == 0) {
                remove_entry(cache, &cache->entries[i]);
            }
        }
    }
}

void vigia_icon_cleanup(void)
{
    cairo_debug_reset_static_data();
    FcFini();
}

void vigia_icon_cache_destroy(VigiaIconCache *cache)
{
    if (cache != NULL) {
        for (size_t i = 0U; i < VIGIA_ICON_CACHE_CAPACITY; i++) {
            remove_entry(cache, &cache->entries[i]);
        }
        free(cache);
        cache_instances--;
        if (cache_instances == 0U) {
            vigia_icon_cleanup();
        }
    }
}
