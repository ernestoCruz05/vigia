#include "vigia_art.h"

#include <curl/curl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vigia_image.h"

typedef struct {
    char url[4096];
    Image image;
    uint64_t age;
} CachedArt;

struct VigiaArtLoader {
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    atomic_bool running;
    atomic_uint_fast64_t active_request;
    uint64_t next_id;
    char active_url[4096];
    bool has_work;
    uint64_t done_request;
    VigiaArtResult result;
    bool has_result;
    CachedArt cache[VIGIA_ART_MAX_CACHE];
    uint64_t cache_clock;
};

typedef struct {
    VigiaArtLoader *loader;
    uint64_t request;
    unsigned char *data;
    size_t size;
} Transfer;

static pthread_once_t curl_once = PTHREAD_ONCE_INIT;
static CURLcode curl_status = CURLE_FAILED_INIT;

static void initialize_curl(void)
{
    curl_status = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (curl_status == CURLE_OK && atexit(curl_global_cleanup) != 0) {
        curl_global_cleanup();
        curl_status = CURLE_FAILED_INIT;
    }
}

static bool cancelled(const Transfer *transfer)
{
    return !atomic_load(&transfer->loader->running)
        || atomic_load(&transfer->loader->active_request) != transfer->request;
}

static char *local_path(const char *url)
{
    CURLU *parsed = curl_url();
    char *host = NULL;
    char *path = NULL;
    if (parsed == NULL) {
        return NULL;
    }
    if (curl_url_set(parsed, CURLUPART_URL, url, 0U) != CURLUE_OK) {
        goto out;
    }
    const CURLUcode host_result = curl_url_get(parsed, CURLUPART_HOST, &host, 0U);
    if (host_result != CURLUE_NO_HOST && host_result != CURLUE_OK) {
        goto out;
    }
    if (host != NULL && host[0] != '\0' && strcmp(host, "localhost") != 0) {
        goto out;
    }
    if (curl_url_get(parsed, CURLUPART_PATH, &path, CURLU_URLDECODE) != CURLUE_OK
        || path == NULL || path[0] != '/') {
        curl_free(path);
        path = NULL;
    }
out:
    curl_free(host);
    curl_url_cleanup(parsed);
    return path;
}

bool vigia_art_is_valid_url(const char *url)
{
    if (url == NULL || strlen(url) >= 4096U) {
        return false;
    }
    if (strncmp(url, "https://", 8U) == 0) {
        return url[8] != '\0';
    }
    if (strncmp(url, "file://", 7U) == 0) {
        char *path = local_path(url);
        const bool valid = path != NULL;
        curl_free(path);
        return valid;
    }
    return false;
}

static size_t receive_bytes(char *data, size_t size, size_t count, void *context)
{
    Transfer *transfer = context;
    if (size != 0U && count > SIZE_MAX / size) {
        return 0U;
    }
    const size_t bytes = size * count;
    if (cancelled(transfer) || bytes > VIGIA_ART_MAX_SIZE - transfer->size) {
        return 0U;
    }
    if (bytes == 0U) {
        return 0U;
    }
    unsigned char *replacement = vigia_image_realloc(transfer->data, transfer->size + bytes);
    if (replacement == NULL) {
        return 0U;
    }
    transfer->data = replacement;
    memcpy(transfer->data + transfer->size, data, bytes);
    transfer->size += bytes;
    return bytes;
}

static int progress(void *context, curl_off_t total, curl_off_t now,
                    curl_off_t upload_total, curl_off_t upload_now)
{
    (void)total;
    (void)now;
    (void)upload_total;
    (void)upload_now;
    return cancelled(context) ? 1 : 0;
}

static Image fetch(Transfer *transfer, const char *url)
{
    Image image = {0};
    if (strncmp(url, "file://", 7U) == 0) {
        char *path = local_path(url);
        if (path != NULL) {
            transfer->data = vigia_image_read_file(path, &transfer->size);
        }
        curl_free(path);
    } else {
        CURL *curl = curl_easy_init();
        if (curl == NULL) {
            return image;
        }
        long code = 0;
        if (curl_easy_setopt(curl, CURLOPT_URL, url) != CURLE_OK
            || curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https") != CURLE_OK
            || curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https") != CURLE_OK
            || curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 3000L) != CURLE_OK
            || curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L) != CURLE_OK
            || curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L) != CURLE_OK
            || curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L) != CURLE_OK
            || curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L) != CURLE_OK
            || curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L) != CURLE_OK
            || curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive_bytes) != CURLE_OK
            || curl_easy_setopt(curl, CURLOPT_WRITEDATA, transfer) != CURLE_OK
            || curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L) != CURLE_OK
            || curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress) != CURLE_OK
            || curl_easy_setopt(curl, CURLOPT_XFERINFODATA, transfer) != CURLE_OK
            || curl_easy_perform(curl) != CURLE_OK
            || curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code) != CURLE_OK
            || code != 200) {
            curl_easy_cleanup(curl);
            vigia_image_free(transfer->data);
            transfer->data = NULL;
            return image;
        }
        curl_easy_cleanup(curl);
    }
    if (!cancelled(transfer) && transfer->data != NULL) {
        image = vigia_image_decode(transfer->data, transfer->size);
    }
    vigia_image_free(transfer->data);
    transfer->data = NULL;
    return image;
}

static Image cached_fetch(Transfer *transfer, const char *url)
{
    VigiaArtLoader *loader = transfer->loader;
    size_t slot = 0U;
    for (size_t i = 0U; i < VIGIA_ART_MAX_CACHE; i++) {
        CachedArt *entry = &loader->cache[i];
        if (entry->image.data != NULL && strcmp(entry->url, url) == 0) {
            entry->age = ++loader->cache_clock;
            Image copy = vigia_image_copy(entry->image);
            if (copy.data == NULL) {
                copy = entry->image;
                *entry = (CachedArt){0};
            }
            return copy;
        }
        if (entry->age < loader->cache[slot].age) {
            slot = i;
        }
    }
    Image image = fetch(transfer, url);
    if (image.data != NULL && !cancelled(transfer)) {
        vigia_image_unload(loader->cache[slot].image);
        loader->cache[slot] = (CachedArt){0};
        Image copy = vigia_image_copy(image);
        if (copy.data != NULL) {
            memcpy(loader->cache[slot].url, url, strlen(url) + 1U);
            loader->cache[slot].image = copy;
            loader->cache[slot].age = ++loader->cache_clock;
        }
    }
    return image;
}

static void *art_worker(void *context)
{
    VigiaArtLoader *loader = context;
    for (;;) {
        pthread_mutex_lock(&loader->mutex);
        while (atomic_load(&loader->running) && !loader->has_work) {
            pthread_cond_wait(&loader->cond, &loader->mutex);
        }
        if (!atomic_load(&loader->running)) {
            pthread_mutex_unlock(&loader->mutex);
            break;
        }
        Transfer transfer = { .loader = loader, .request = atomic_load(&loader->active_request) };
        char url[4096] = {0};
        memcpy(url, loader->active_url, sizeof url);
        loader->has_work = false;
        pthread_mutex_unlock(&loader->mutex);
        Image image = cached_fetch(&transfer, url);
        pthread_mutex_lock(&loader->mutex);
        if (!cancelled(&transfer)) {
            vigia_image_unload(loader->result.image);
            loader->result = (VigiaArtResult){ .image = image, .ready = true,
                .success = image.data != NULL, .generation = transfer.request };
            loader->done_request = transfer.request;
            loader->has_result = true;
        } else {
            vigia_image_unload(image);
        }
        pthread_mutex_unlock(&loader->mutex);
    }
    return NULL;
}

VigiaArtLoader *vigia_art_loader_create(void)
{
    if (pthread_once(&curl_once, initialize_curl) != 0 || curl_status != CURLE_OK) {
        return NULL;
    }
    VigiaArtLoader *loader = calloc(1U, sizeof *loader);
    if (loader == NULL) {
        return NULL;
    }
    atomic_init(&loader->running, true);
    atomic_init(&loader->active_request, 0U);
    loader->next_id = 1U;
    if (pthread_mutex_init(&loader->mutex, NULL) != 0) {
        free(loader);
        return NULL;
    }
    if (pthread_cond_init(&loader->cond, NULL) != 0) {
        pthread_mutex_destroy(&loader->mutex);
        free(loader);
        return NULL;
    }
    if (pthread_create(&loader->thread, NULL, art_worker, loader) != 0) {
        pthread_cond_destroy(&loader->cond);
        pthread_mutex_destroy(&loader->mutex);
        free(loader);
        return NULL;
    }
    return loader;
}

uint64_t vigia_art_loader_request(VigiaArtLoader *loader, const char *url)
{
    if (loader == NULL || !vigia_art_is_valid_url(url)) {
        return 0U;
    }
    pthread_mutex_lock(&loader->mutex);
    const uint64_t request = loader->next_id++;
    if (request != 0U) {
        atomic_store(&loader->active_request, request);
        memcpy(loader->active_url, url, strlen(url) + 1U);
        loader->has_work = true;
        pthread_cond_signal(&loader->cond);
    }
    pthread_mutex_unlock(&loader->mutex);
    return request;
}

bool vigia_art_loader_poll(VigiaArtLoader *loader, uint64_t request, VigiaArtResult *result)
{
    if (loader == NULL || result == NULL || request == 0U) {
        return false;
    }
    pthread_mutex_lock(&loader->mutex);
    const bool found = loader->has_result && loader->done_request == request;
    if (found) {
        *result = loader->result;
        loader->result = (VigiaArtResult){0};
        loader->has_result = false;
    }
    pthread_mutex_unlock(&loader->mutex);
    return found;
}

void vigia_art_loader_cancel(VigiaArtLoader *loader, uint64_t request)
{
    if (loader == NULL) {
        return;
    }
    pthread_mutex_lock(&loader->mutex);
    if (atomic_load(&loader->active_request) == request) {
        atomic_store(&loader->active_request, 0U);
        loader->has_work = false;
    }
    if (loader->done_request == request) {
        vigia_image_unload(loader->result.image);
        loader->result = (VigiaArtResult){0};
        loader->has_result = false;
    }
    pthread_mutex_unlock(&loader->mutex);
}

void vigia_art_result_free(VigiaArtResult *result)
{
    if (result != NULL) {
        vigia_image_unload(result->image);
        *result = (VigiaArtResult){0};
    }
}

void vigia_art_loader_destroy(VigiaArtLoader *loader)
{
    if (loader == NULL) {
        return;
    }
    pthread_mutex_lock(&loader->mutex);
    atomic_store(&loader->running, false);
    pthread_cond_signal(&loader->cond);
    pthread_mutex_unlock(&loader->mutex);
    if (pthread_join(loader->thread, NULL) != 0) {
        abort();
    }
    vigia_image_unload(loader->result.image);
    for (size_t i = 0U; i < VIGIA_ART_MAX_CACHE; i++) {
        vigia_image_unload(loader->cache[i].image);
    }
    pthread_cond_destroy(&loader->cond);
    pthread_mutex_destroy(&loader->mutex);
    free(loader);
}
