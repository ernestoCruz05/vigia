#ifndef VIGIA_MUSIC_H
#define VIGIA_MUSIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "raylib.h"
#include "vigia_art.h"
#include "vigia_draw.h"
#include "vigia_layout.h"
#include "vigia_module.h"
#include "vigia_mpris.h"

#define VIGIA_MUSIC_CARD_WIDTH 208
#define VIGIA_MUSIC_CARD_HEIGHT 256
#define VIGIA_MUSIC_MARGIN 12
#define VIGIA_MUSIC_COVER_SIZE 184
#define VIGIA_MUSIC_DEFAULT_DISPLAY_MS 5000
#define VIGIA_MUSIC_FADE_SECONDS 0.180f

typedef struct VigiaMusicState {
    bool presented_visible;
    float presented_opacity;
    VigiaRect presented_rect;
    Texture2D presented_texture;
    char presented_title[4096];
    char presented_artist[4096];
    bool enabled;
    bool active;
    float elapsed_seconds;
    float display_duration_seconds;
    float start_alpha;
    char current_track_id[4096];
    char current_uri[4096];
    char current_object_path[4096];
    char title[4096];
    char artist[4096];
    char art_url[4096];
    uint64_t art_request_id;
    Texture2D cover_texture;
    size_t cover_bytes;
    bool has_texture;
    VigiaMpris *mpris;
    VigiaArtLoader *art_loader;
    VigiaMpris *prepared_mpris;
    VigiaArtLoader *prepared_art;
    bool prepared;
    VigiaHost *host;
    int screen_width;
    int screen_height;
    bool launcher_enabled;
    int launcher_x;
    int launcher_width;
    int launcher_height;
    VigiaRect toast_rect;
} VigiaMusicState;

void vigia_music_state_init(VigiaMusicState *state);
void vigia_music_state_cleanup(VigiaMusicState *state);

VigiaRect vigia_music_compute_layout(int screen_width, int screen_height,
                                    bool launcher_enabled, int launcher_x,
                                    int launcher_width, int launcher_height);

bool vigia_music_is_suppressed(int screen_width, int screen_height,
                               VigiaRect toast_rect, VigiaRect card_rect);

float vigia_music_get_opacity(const VigiaMusicState *state);

void vigia_music_trigger_track(VigiaMusicState *state, const VigiaSpotifyTrack *track);

void vigia_music_update_state(VigiaMusicState *state, float delta_seconds);

size_t vigia_music_get_input_regions(const VigiaMusicState *state,
                                     VigiaRect *regions, size_t capacity);

void vigia_music_render_state(VigiaMusicState *state, const VigiaDraw *draw,
                              VigiaRect card_rect);

VigiaModule *vigia_music_module_create(void);

#endif
