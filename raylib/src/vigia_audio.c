#include "vigia_audio.h"

#include <pulse/pulseaudio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct VigiaAudio {
    pa_mainloop *loop;
    pa_context *context;
    pa_operation *operation;
    VigiaAudioCallback callback;
    void *data;
    char sink[1024];
    pa_cvolume volume;
    uint32_t sink_index;
    bool muted;
    bool baseline;
    bool dirty;
    bool ready;
    bool failed;
    bool need_sink;
    double retry_at;
};

static double now(void)
{
    struct timespec value = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
        return 0.0;
    }
    return (double)value.tv_sec + (double)value.tv_nsec / 1000000000.0;
}

static void disconnect_audio(VigiaAudio *audio)
{
    if (audio->operation != NULL) {
        pa_operation_cancel(audio->operation);
        pa_operation_unref(audio->operation);
        audio->operation = NULL;
    }
    if (audio->context != NULL) {
        pa_context_set_state_callback(audio->context, NULL, NULL);
        pa_context_set_subscribe_callback(audio->context, NULL, NULL);
        pa_context_disconnect(audio->context);
        pa_context_unref(audio->context);
        audio->context = NULL;
    }
    audio->baseline = false;
    audio->ready = false;
    audio->need_sink = false;
    audio->dirty = true;
    audio->failed = false;
    audio->sink[0] = '\0';
    audio->retry_at = now() + 1.0;
}

static void sink_info(pa_context *context, const pa_sink_info *info, int end, void *data)
{
    (void)context;
    VigiaAudio *audio = data;
    if (end < 0) {
        audio->baseline = false;
        return;
    }
    if (end != 0 || info == NULL || info->name == NULL
        || strcmp(info->name, audio->sink) != 0 || !pa_cvolume_valid(&info->volume)) {
        return;
    }
    const bool muted = info->mute != 0;
    const bool changed = audio->baseline && audio->sink_index == info->index
        && (!pa_cvolume_equal(&audio->volume, &info->volume) || muted != audio->muted);
    audio->volume = info->volume;
    audio->sink_index = info->index;
    audio->muted = muted;
    audio->baseline = true;
    if (changed && audio->callback != NULL) {
        const double percent = (double)pa_cvolume_avg(&info->volume) * 100.0 / (double)PA_VOLUME_NORM;
        const unsigned int volume = percent >= 1000.0 ? 1000U : (unsigned int)(percent + 0.5);
        audio->callback(volume, muted, audio->data);
    }
}

static void server_info(pa_context *context, const pa_server_info *info, void *data)
{
    (void)context;
    VigiaAudio *audio = data;
    if (info == NULL || info->default_sink_name == NULL
        || strlen(info->default_sink_name) >= sizeof audio->sink) {
        audio->baseline = false;
        audio->sink[0] = '\0';
        return;
    }
    if (strcmp(audio->sink, info->default_sink_name) != 0) {
        audio->baseline = false;
        memcpy(audio->sink, info->default_sink_name, strlen(info->default_sink_name) + 1U);
    }
    audio->need_sink = audio->sink[0] != '\0';
}

static void subscribed(pa_context *context, int success, void *data)
{
    (void)context;
    VigiaAudio *audio = data;
    audio->failed = success == 0;
    audio->dirty = true;
}

static void event(pa_context *context, pa_subscription_event_type_t type, uint32_t index, void *data)
{
    (void)context;
    (void)index;
    VigiaAudio *audio = data;
    const pa_subscription_event_type_t facility = type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
    if (facility == PA_SUBSCRIPTION_EVENT_SERVER || facility == PA_SUBSCRIPTION_EVENT_SINK) {
        audio->dirty = true;
    }
}

static void state_changed(pa_context *context, void *data)
{
    VigiaAudio *audio = data;
    const pa_context_state_t state = pa_context_get_state(context);
    if (state == PA_CONTEXT_READY) {
        audio->ready = true;
        pa_context_set_subscribe_callback(context, event, audio);
        audio->operation = pa_context_subscribe(context,
            PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SERVER, subscribed, audio);
        audio->failed = audio->operation == NULL;
    } else if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
        audio->failed = true;
    }
}

VigiaAudio *vigia_audio_create(VigiaAudioCallback callback, void *data)
{
    VigiaAudio *audio = calloc(1U, sizeof *audio);
    if (audio == NULL) {
        return NULL;
    }
    audio->loop = pa_mainloop_new();
    if (audio->loop == NULL) {
        free(audio);
        return NULL;
    }
    audio->callback = callback;
    audio->data = data;
    audio->dirty = true;
    return audio;
}

void vigia_audio_poll(VigiaAudio *audio)
{
    if (audio == NULL) {
        return;
    }
    if (audio->context == NULL && now() >= audio->retry_at) {
        audio->context = pa_context_new(pa_mainloop_get_api(audio->loop), "Vigia volume observer");
        if (audio->context == NULL) {
            audio->retry_at = now() + 1.0;
            return;
        }
        pa_context_set_state_callback(audio->context, state_changed, audio);
        if (pa_context_connect(audio->context, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL) < 0) {
            disconnect_audio(audio);
            return;
        }
    }
    for (unsigned int i = 0U; i < 8U; i++) {
        const int result = pa_mainloop_iterate(audio->loop, 0, NULL);
        if (result < 0) {
            audio->failed = true;
        }
        if (result <= 0 || audio->failed) {
            break;
        }
    }
    if (audio->failed) {
        disconnect_audio(audio);
        return;
    }
    if (audio->operation != NULL && pa_operation_get_state(audio->operation) != PA_OPERATION_RUNNING) {
        pa_operation_unref(audio->operation);
        audio->operation = NULL;
    }
    if (!audio->ready || audio->operation != NULL) {
        return;
    }
    if (audio->need_sink) {
        audio->need_sink = false;
        audio->operation = pa_context_get_sink_info_by_name(audio->context, audio->sink, sink_info, audio);
        audio->failed = audio->operation == NULL;
    } else if (audio->dirty) {
        audio->dirty = false;
        audio->operation = pa_context_get_server_info(audio->context, server_info, audio);
        audio->failed = audio->operation == NULL;
    }
}

void vigia_audio_destroy(VigiaAudio *audio)
{
    if (audio != NULL) {
        disconnect_audio(audio);
        pa_mainloop_free(audio->loop);
        free(audio);
    }
}
