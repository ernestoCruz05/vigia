#include "vigia_music.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vigia_host.h"
#include "vigia_image.h"
#include "vigia_text.h"

static float cubic_out(float p)
{
    const float t = 1.0f - p;
    return 1.0f - t * t * t;
}

void vigia_music_state_init(VigiaMusicState *state)
{
    if (state == NULL) {
        return;
    }
    *state = (VigiaMusicState){
        .enabled = true,
        .active = false,
        .elapsed_seconds = 0.0f,
        .display_duration_seconds = (float)VIGIA_MUSIC_DEFAULT_DISPLAY_MS / 1000.0f,
        .start_alpha = 0.0f,
        .current_track_id = {0},
        .title = {0},
        .artist = {0},
        .art_url = {0},
        .art_request_id = 0U,
        .cover_texture = {0},
        .has_texture = false,
        .mpris = NULL,
        .art_loader = NULL,
        .host = NULL,
        .screen_width = 0,
        .screen_height = 0,
        .launcher_enabled = false,
        .launcher_x = 0,
        .launcher_width = 0,
        .launcher_height = 0,
        .toast_rect = (VigiaRect){0},
    };
}

static void release_cover(VigiaMusicState *state)
{
    if (state->has_texture && IsWindowReady()) {
        UnloadTexture(state->cover_texture);
    }
    vigia_image_release(state->cover_bytes);
    state->cover_bytes = 0U;
    state->cover_texture = (Texture2D){0};
    state->has_texture = false;
}

void vigia_music_state_cleanup(VigiaMusicState *state)
{
    if (state == NULL) {
        return;
    }
    release_cover(state);
    state->active = false;
    vigia_mpris_destroy(state->prepared_mpris);
    vigia_art_loader_destroy(state->prepared_art);
    state->prepared_mpris = NULL;
    state->prepared_art = NULL;
    state->prepared = false;
    if (state->art_loader != NULL) {
        if (state->art_request_id > 0U) {
            vigia_art_loader_cancel(state->art_loader, state->art_request_id);
            state->art_request_id = 0U;
        }
        vigia_art_loader_destroy(state->art_loader);
        state->art_loader = NULL;
    }
    if (state->mpris != NULL) {
        if (state->host != NULL && state->host->mpris == state->mpris) {
            vigia_mpris_set_spotify_callback(state->mpris, NULL, NULL);
        } else {
            vigia_mpris_destroy(state->mpris);
        }
        state->mpris = NULL;
    }
}

VigiaRect vigia_music_compute_layout(int screen_width, int screen_height,
                                    bool launcher_enabled, int launcher_x,
                                    int launcher_width, int launcher_height)
{
    (void)screen_width;
    VigiaRect rect = {
        .x = VIGIA_MUSIC_MARGIN,
        .y = screen_height - VIGIA_MUSIC_MARGIN - VIGIA_MUSIC_CARD_HEIGHT,
        .width = VIGIA_MUSIC_CARD_WIDTH,
        .height = VIGIA_MUSIC_CARD_HEIGHT,
    };
    if (launcher_enabled) {
        const int card_right = rect.x + rect.width;
        const int launcher_right = launcher_x + launcher_width;
        if (launcher_x < card_right && launcher_right > rect.x) {
            const int launcher_y = screen_height - VIGIA_MUSIC_MARGIN - launcher_height;
            rect.y = launcher_y - VIGIA_MUSIC_MARGIN - VIGIA_MUSIC_CARD_HEIGHT;
        }
    }
    return rect;
}

bool vigia_music_is_suppressed(int screen_width, int screen_height,
                               VigiaRect toast_rect, VigiaRect card_rect)
{
    if (card_rect.x < 0 || card_rect.y < 0) {
        return true;
    }
    if (card_rect.x + card_rect.width > screen_width) {
        return true;
    }
    if (card_rect.y + card_rect.height > screen_height) {
        return true;
    }
    if (toast_rect.width > 0 && toast_rect.height > 0) {
        const bool x_overlap = card_rect.x < toast_rect.x + toast_rect.width &&
                              card_rect.x + card_rect.width > toast_rect.x;
        const bool y_overlap = card_rect.y < toast_rect.y + toast_rect.height &&
                              card_rect.y + card_rect.height > toast_rect.y;
        if (x_overlap && y_overlap) {
            return true;
        }
    }
    return false;
}

float vigia_music_get_opacity(const VigiaMusicState *state)
{
    if (state == NULL || !state->active) {
        return 0.0f;
    }
    const float t = state->elapsed_seconds;
    const float fade_in_dur = VIGIA_MUSIC_FADE_SECONDS;
    const float display_dur = state->display_duration_seconds > 0.0f
        ? state->display_duration_seconds
        : (float)VIGIA_MUSIC_DEFAULT_DISPLAY_MS / 1000.0f;
    const float fade_out_dur = VIGIA_MUSIC_FADE_SECONDS;

    if (t < fade_in_dur) {
        const float p = t / fade_in_dur;
        return state->start_alpha + (1.0f - state->start_alpha) * cubic_out(p);
    }
    if (t <= display_dur) {
        return 1.0f;
    }
    if (t <= display_dur + fade_out_dur) {
        const float p = (t - display_dur) / fade_out_dur;
        return 1.0f - cubic_out(p);
    }
    return 0.0f;
}

void vigia_music_trigger_track(VigiaMusicState *state, const VigiaSpotifyTrack *track)
{
    if (state == NULL) {
        return;
    }
    if (track == NULL || track->title[0] == '\0' || !track->is_playing ||
        track->track_id[0] == '\0' ||
        strcmp(track->track_id, "/org/mpris/MediaPlayer2/TrackList/NoTrack") == 0) {
        state->active = false;
        state->elapsed_seconds = 0.0f;
        state->start_alpha = 0.0f;
        release_cover(state);
        if (state->art_loader != NULL && state->art_request_id > 0U) {
            vigia_art_loader_cancel(state->art_loader, state->art_request_id);
            state->art_request_id = 0U;
        }
        return;
    }

    const float current_alpha = vigia_music_get_opacity(state);
    state->start_alpha = current_alpha;
    state->elapsed_seconds = 0.0f;
    state->active = true;

    (void)snprintf(state->current_track_id, sizeof state->current_track_id, "%s", track->track_id);
    memcpy(state->current_uri, track->uri, sizeof state->current_uri);
    memcpy(state->current_object_path, track->object_path, sizeof state->current_object_path);
    (void)snprintf(state->title, sizeof state->title, "%s", track->title);
    (void)snprintf(state->artist, sizeof state->artist, "%s", track->artist);
    (void)snprintf(state->art_url, sizeof state->art_url, "%s", track->art_url);

    release_cover(state);
    if (state->art_loader != NULL && state->art_request_id > 0U) {
        vigia_art_loader_cancel(state->art_loader, state->art_request_id);
        state->art_request_id = 0U;
    }
    if (state->art_loader != NULL && track->art_url[0] != '\0') {
        state->art_request_id = vigia_art_loader_request(state->art_loader, track->art_url);
    }
}

void vigia_music_update_state(VigiaMusicState *state, float delta_seconds)
{
    if (state == NULL) {
        return;
    }
    if (state->active) {
        state->elapsed_seconds += delta_seconds;
    }
    const float display_dur = state->display_duration_seconds > 0.0f
        ? state->display_duration_seconds
        : (float)VIGIA_MUSIC_DEFAULT_DISPLAY_MS / 1000.0f;
    if (state->active && state->elapsed_seconds >= display_dur + VIGIA_MUSIC_FADE_SECONDS) {
        vigia_music_trigger_track(state, NULL);
    }
    if (state->mpris != NULL && state->host == NULL) {
        vigia_mpris_poll(state->mpris);
    }
    if (!state->active) {
        return;
    }
    if (state->art_loader != NULL && state->art_request_id > 0U && !state->has_texture) {
        VigiaArtResult result = {0};
        if (vigia_art_loader_poll(state->art_loader, state->art_request_id, &result)) {
            if (result.ready && result.success && result.image.data != NULL && IsWindowReady()) {
                const size_t bytes = (size_t)result.image.width * (size_t)result.image.height * 4U;
                if (vigia_image_reserve(bytes)) {
                    state->cover_texture = LoadTextureFromImage(result.image);
                    state->has_texture = state->cover_texture.id != 0U;
                    if (state->has_texture) {
                        state->cover_bytes = bytes;
                    } else {
                        vigia_image_release(bytes);
                    }
                }
            }
            vigia_art_result_free(&result);
            state->art_request_id = 0U;
        }
    }
}

size_t vigia_music_get_input_regions(const VigiaMusicState *state,
                                     VigiaRect *regions, size_t capacity)
{
    (void)state;
    (void)regions;
    (void)capacity;
    return 0U;
}

void vigia_music_render_state(VigiaMusicState *state, const VigiaDraw *draw,
                              VigiaRect card_rect)
{
    if (state == NULL || !state->active) {
        return;
    }
    const float alpha = vigia_music_get_opacity(state);
    if (alpha <= 0.001f) {
        return;
    }
    vigia_draw_surface(card_rect, alpha);

    const VigiaRect cover_box = {
        .x = card_rect.x + VIGIA_MUSIC_MARGIN,
        .y = card_rect.y + VIGIA_MUSIC_MARGIN,
        .width = VIGIA_MUSIC_COVER_SIZE,
        .height = VIGIA_MUSIC_COVER_SIZE,
    };

    if (state->has_texture && state->cover_texture.id > 0U) {
        const float scale = fminf(
            (float)VIGIA_MUSIC_COVER_SIZE / (float)state->cover_texture.width,
            (float)VIGIA_MUSIC_COVER_SIZE / (float)state->cover_texture.height);
        const float dw = (float)state->cover_texture.width * scale;
        const float dh = (float)state->cover_texture.height * scale;
        const float dx = (float)cover_box.x + ((float)VIGIA_MUSIC_COVER_SIZE - dw) * 0.5f;
        const float dy = (float)cover_box.y + ((float)VIGIA_MUSIC_COVER_SIZE - dh) * 0.5f;
        const Rectangle src = { 0.0f, 0.0f, (float)state->cover_texture.width, (float)state->cover_texture.height };
        const Rectangle dst = { dx, dy, dw, dh };
        DrawTexturePro(state->cover_texture, src, dst, (Vector2){0}, 0.0f, vigia_color(0xFFFFFFFFU, alpha));
    } else {
        const Rectangle placeholder = {
            .x = (float)cover_box.x,
            .y = (float)cover_box.y,
            .width = (float)cover_box.width,
            .height = (float)cover_box.height,
        };
        DrawRectangleRec(placeholder, vigia_color(0x382C2CFFU, alpha));
        DrawRectangleLinesEx(placeholder, 1.0f, vigia_color(VIGIA_KEYLINE_RGBA, alpha));
    }

    if (draw == NULL || draw->font.texture.id == 0U) {
        return;
    }

    char folded_title[VIGIA_TEXT_LINE_CAP] = {0};
    (void)vigia_text_fold(folded_title, sizeof folded_title, state->title);
    vigia_text_truncate(folded_title, 23U);
    const Vector2 title_pos = {
        .x = (float)(card_rect.x + VIGIA_MUSIC_MARGIN),
        .y = (float)(card_rect.y + VIGIA_MUSIC_MARGIN + VIGIA_MUSIC_COVER_SIZE + 8),
    };
    DrawTextEx(draw->font, folded_title, title_pos, (float)VIGIA_FONT_SIZE, 0.0f,
               vigia_color(VIGIA_FOREGROUND_RGBA, alpha));

    if (state->artist[0] != '\0') {
        char folded_artist[VIGIA_TEXT_LINE_CAP] = {0};
        (void)vigia_text_fold(folded_artist, sizeof folded_artist, state->artist);
        vigia_text_truncate(folded_artist, 23U);
        const Vector2 artist_pos = {
            .x = (float)(card_rect.x + VIGIA_MUSIC_MARGIN),
            .y = (float)(card_rect.y + VIGIA_MUSIC_MARGIN + VIGIA_MUSIC_COVER_SIZE + 28),
        };
        DrawTextEx(draw->font, folded_artist, artist_pos, (float)VIGIA_FONT_SIZE, 0.0f,
                   vigia_color(VIGIA_DIM_RGBA, alpha));
    }
}

static bool card_layout(const VigiaMusicState *state, VigiaRect *rect)
{
    const int width = IsWindowReady() ? GetScreenWidth()
        : (state->host != NULL ? state->host->screen_width : state->screen_width);
    const int height = IsWindowReady() ? GetScreenHeight()
        : (state->host != NULL ? state->host->screen_height : state->screen_height);
    VigiaRect reserved = {0};
    VigiaRect notifications = {0};
    if (state->host != NULL) {
        reserved = state->host->launcher_footprint;
        if (state->host->config.notifications_enabled) {
            if (state->host->state.recalling) {
                notifications = state->host->layout.recall;
            } else if (state->host->state.active_count > 0U) {
                notifications = state->host->layout.toast;
                notifications.height = state->host->layout.toast_stack_height;
            }
        }
    }
    *rect = vigia_music_compute_layout(width, height, reserved.width > 0,
        reserved.x, reserved.width, reserved.height);
    return !vigia_music_is_suppressed(width, height, notifications, *rect);
}

static bool displayed_track(const VigiaMusicState *state, const VigiaSpotifyTrack *track)
{
    if (state->current_uri[0] != '\0' && track->uri[0] != '\0') {
        return vigia_mpris_track_ids_equal(state->current_uri, track->uri);
    }
    if (state->current_object_path[0] != '\0' && track->object_path[0] != '\0') {
        return strcmp(state->current_object_path, track->object_path) == 0;
    }
    return vigia_mpris_track_ids_equal(state->current_track_id, track->track_id);
}

static void on_spotify_track(const VigiaSpotifyTrack *track, bool is_new_track, void *user_data)
{
    VigiaMusicState *state = user_data;
    if (state == NULL) {
        return;
    }
    VigiaRect rect = {0};
    if (track == NULL || !state->enabled || !card_layout(state, &rect)) {
        vigia_music_trigger_track(state, NULL);
        return;
    }
    if (is_new_track) {
        vigia_music_trigger_track(state, track);
    } else if (state->active && displayed_track(state, track)) {
        memcpy(state->current_uri, track->uri, sizeof state->current_uri);
        memcpy(state->current_object_path, track->object_path, sizeof state->current_object_path);
        if (track->title[0] != '\0') {
            (void)snprintf(state->title, sizeof state->title, "%s", track->title);
        }
        if (track->artist[0] != '\0') {
            (void)snprintf(state->artist, sizeof state->artist, "%s", track->artist);
        }
        if (track->art_url[0] != '\0' && strcmp(track->art_url, state->art_url) != 0) {
            (void)snprintf(state->art_url, sizeof state->art_url, "%s", track->art_url);
            release_cover(state);
            if (state->art_loader != NULL && state->art_request_id > 0U) {
                vigia_art_loader_cancel(state->art_loader, state->art_request_id);
                state->art_request_id = 0U;
            }
            if (state->art_loader != NULL) {
                state->art_request_id = vigia_art_loader_request(state->art_loader, state->art_url);
            }
        }
    }
}

static bool module_init(VigiaModule *self, VigiaHost *host)
{
    VigiaMusicState *state = calloc(1, sizeof *state);
    if (state == NULL) {
        return false;
    }
    vigia_music_state_init(state);
    state->host = host;
    state->enabled = false;
    self->state = state;
    return true;
}

static void module_shutdown(VigiaModule *self)
{
    if (self == NULL || self->state == NULL) {
        return;
    }
    VigiaMusicState *state = self->state;
    vigia_music_state_cleanup(state);
    free(state);
    self->state = NULL;
}

static void module_abort_config(VigiaModule *self)
{
    VigiaMusicState *state = self != NULL ? self->state : NULL;
    if (state != NULL) {
        vigia_mpris_destroy(state->prepared_mpris);
        vigia_art_loader_destroy(state->prepared_art);
        state->prepared_mpris = NULL;
        state->prepared_art = NULL;
        state->prepared = false;
    }
}

static bool module_prepare_config(VigiaModule *self, const VigiaConfig *candidate)
{
    VigiaMusicState *state = self != NULL ? self->state : NULL;
    if (state == NULL || candidate == NULL) {
        return false;
    }
    module_abort_config(self);
    if (candidate->music.enabled && state->mpris == NULL
        && (state->host == NULL || !state->host->demo)) {
        state->prepared_art = vigia_art_loader_create();
        if (state->host == NULL || state->host->mpris == NULL) {
            state->prepared_mpris = vigia_mpris_create(on_spotify_track, state);
        }
        if (state->prepared_art == NULL
            || (state->prepared_mpris == NULL && (state->host == NULL || state->host->mpris == NULL))) {
            module_abort_config(self);
            return false;
        }
    }
    state->prepared = true;
    return true;
}

static void module_apply_config(VigiaModule *self, const VigiaConfig *config)
{
    if (self == NULL || self->state == NULL || config == NULL) {
        return;
    }
    VigiaMusicState *state = self->state;
    if (!state->prepared && !module_prepare_config(self, config)) {
        return;
    }
    if (!config->music.enabled || (state->host != NULL && state->host->demo)) {
        vigia_music_state_cleanup(state);
    } else if (state->prepared_art != NULL) {
        state->art_loader = state->prepared_art;
        if (state->host != NULL) {
            if (state->host->mpris == NULL) {
                state->host->mpris = state->prepared_mpris;
            } else {
                vigia_mpris_destroy(state->prepared_mpris);
            }
            state->mpris = state->host->mpris;
            vigia_mpris_set_spotify_callback(state->mpris, on_spotify_track, state);
        } else {
            state->mpris = state->prepared_mpris;
        }
        state->prepared_art = NULL;
        state->prepared_mpris = NULL;
    }
    state->prepared = false;
    state->enabled = config->music.enabled;
    if (config->music.display_ms > 0) {
        state->display_duration_seconds = (float)config->music.display_ms / 1000.0f;
    }
    state->launcher_enabled = config->launcher.enabled;
}

static void module_update(VigiaModule *self, float delta_time)
{
    if (self == NULL || self->state == NULL) {
        return;
    }
    VigiaMusicState *state = self->state;
    if (!state->enabled) {
        return;
    }
    VigiaRect rect = {0};
    if (state->active && !card_layout(state, &rect)) {
        vigia_music_trigger_track(state, NULL);
    }
    vigia_music_update_state(state, delta_time);
}

static size_t module_get_input_regions(const VigiaModule *self, VigiaRect *regions, size_t capacity)
{
    if (self == NULL || self->state == NULL) {
        return 0U;
    }
    const VigiaMusicState *state = self->state;
    return vigia_music_get_input_regions(state, regions, capacity);
}

static bool module_presentation_changed(VigiaModule *self)
{
    VigiaMusicState *state = self->state;
    if (state == NULL) {
        return false;
    }
    VigiaRect rect = {0};
    const float opacity = vigia_music_get_opacity(state);
    const bool visible = state->enabled && state->active && opacity > 0.001F
        && card_layout(state, &rect);
    const Texture2D texture = state->has_texture ? state->cover_texture : (Texture2D){0};
    const bool changed = visible != state->presented_visible || (visible
        && (opacity != state->presented_opacity
            || rect.x != state->presented_rect.x || rect.y != state->presented_rect.y
            || rect.width != state->presented_rect.width || rect.height != state->presented_rect.height
            || texture.id != state->presented_texture.id
            || texture.width != state->presented_texture.width
            || texture.height != state->presented_texture.height
            || strcmp(state->title, state->presented_title) != 0
            || strcmp(state->artist, state->presented_artist) != 0));
    state->presented_visible = visible;
    state->presented_opacity = opacity;
    state->presented_rect = rect;
    state->presented_texture = texture;
    if (changed && visible) {
        memcpy(state->presented_title, state->title, sizeof state->presented_title);
        memcpy(state->presented_artist, state->artist, sizeof state->presented_artist);
    }
    return changed;
}

static void module_render(VigiaModule *self, VigiaDraw *draw)
{
    if (self == NULL || self->state == NULL) {
        return;
    }
    VigiaMusicState *state = self->state;
    if (!state->enabled || !state->active) {
        return;
    }
    VigiaRect rect = {0};
    if (!card_layout(state, &rect)) {
        return;
    }
    vigia_music_render_state(state, draw, rect);
}

static bool module_handle_pointer(VigiaModule *self, int x, int y, bool is_down, bool is_released)
{
    (void)self;
    (void)x;
    (void)y;
    (void)is_down;
    (void)is_released;
    return false;
}

VigiaModule *vigia_music_module_create(void)
{
    VigiaModule *mod = calloc(1, sizeof *mod);
    if (mod == NULL) {
        return NULL;
    }
    mod->name = "music";
    mod->init = module_init;
    mod->shutdown = module_shutdown;
    mod->prepare_config = module_prepare_config;
    mod->abort_config = module_abort_config;
    mod->apply_config = module_apply_config;
    mod->update = module_update;
    mod->get_input_regions = module_get_input_regions;
    mod->presentation_changed = module_presentation_changed;
    mod->render = module_render;
    mod->handle_pointer = module_handle_pointer;
    return mod;
}
