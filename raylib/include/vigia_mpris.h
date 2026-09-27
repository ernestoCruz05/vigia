#ifndef VIGIA_MPRIS_H
#define VIGIA_MPRIS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VIGIA_MPRIS_TEXT_CAP 4096U

typedef struct {
    char track_id[VIGIA_MPRIS_TEXT_CAP];
    char uri[VIGIA_MPRIS_TEXT_CAP];
    char object_path[VIGIA_MPRIS_TEXT_CAP];
    char title[VIGIA_MPRIS_TEXT_CAP];
    char artist[VIGIA_MPRIS_TEXT_CAP];
    char art_url[VIGIA_MPRIS_TEXT_CAP];
    bool is_playing;
    uint64_t generation;
} VigiaSpotifyTrack;

typedef void (*VigiaSpotifyCallback)(const VigiaSpotifyTrack *track, bool is_new_track, void *user_data);

typedef struct VigiaMpris VigiaMpris;

VigiaMpris *vigia_mpris_create(VigiaSpotifyCallback callback, void *user_data);
typedef void (*VigiaMediaCallback)(const VigiaSpotifyTrack *track, void *user_data);
void vigia_mpris_set_media_callback(VigiaMpris *mpris, VigiaMediaCallback callback, void *user_data);
void vigia_mpris_set_spotify_callback(VigiaMpris *mpris, VigiaSpotifyCallback callback, void *user_data);
void vigia_mpris_poll(VigiaMpris *mpris);
void vigia_mpris_destroy(VigiaMpris *mpris);

bool vigia_mpris_is_valid_track_id(const char *track_id);
bool vigia_mpris_track_ids_equal(const char *id1, const char *id2);
bool vigia_mpris_normalize_spotify_id(const char *input, char *out, size_t capacity);

#endif
