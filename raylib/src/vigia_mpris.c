#include "vigia_mpris.h"

#include <dbus/dbus.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SPOTIFY_BUS_NAME "org.mpris.MediaPlayer2.spotify"
#define SPOTIFY_OBJECT_PATH "/org/mpris/MediaPlayer2"
#define MPRIS_PLAYER_IFACE "org.mpris.MediaPlayer2.Player"

struct VigiaMpris {
    DBusConnection *conn;
    VigiaMpris *parent;
    VigiaMpris *players[16];
    char name[256];
    DBusPendingCall *listing;
    bool list_needed;
    VigiaMediaCallback media_callback;
    void *media_data;
    bool media_published;
    bool media_present;
    VigiaSpotifyTrack media_previous;
    DBusPendingCall *pending;
    bool resolving_owner;
    bool lookup_needed;
    bool snapshot_needed;
    bool baseline;
    char owner[128];
    uint64_t owner_generation;
    uint64_t pending_generation;
    uint64_t generation;
    double retry_at;
    VigiaSpotifyTrack current;
    VigiaSpotifyTrack reference;
    VigiaSpotifyCallback callback;
    void *user_data;
};

static double monotonic_time(void)
{
    struct timespec value = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
        return 0.0;
    }
    return (double)value.tv_sec + (double)value.tv_nsec / 1000000000.0;
}

bool vigia_mpris_is_valid_track_id(const char *id)
{
    return id != NULL && id[0] != '\0' && strcmp(id, "/") != 0
        && strcmp(id, "/org/mpris/MediaPlayer2/TrackList/NoTrack") != 0;
}

bool vigia_mpris_normalize_spotify_id(const char *input, char *output, size_t capacity)
{
    if (input == NULL || output == NULL || capacity == 0U) {
        return false;
    }
    const char *resource = NULL;
    const char *uri_prefix = "spotify:";
    const char *url_prefix = "https://open.spotify.com/";
    if (strncmp(input, uri_prefix, strlen(uri_prefix)) == 0) {
        resource = input + strlen(uri_prefix);
    } else if (strncmp(input, url_prefix, strlen(url_prefix)) == 0) {
        resource = input + strlen(url_prefix);
    } else {
        return false;
    }
    size_t length = strcspn(resource, "?#");
    if (length == 0U || length >= capacity) {
        return false;
    }
    memcpy(output, resource, length);
    output[length] = '\0';
    char *separator = strchr(output, '/');
    if (separator != NULL) {
        *separator = ':';
    }
    separator = strchr(output, ':');
    return separator != NULL && separator != output && separator[1] != '\0';
}

bool vigia_mpris_track_ids_equal(const char *first, const char *second)
{
    if (first == NULL || second == NULL) {
        return false;
    }
    char a[VIGIA_MPRIS_TEXT_CAP] = {0};
    char b[VIGIA_MPRIS_TEXT_CAP] = {0};
    if (vigia_mpris_normalize_spotify_id(first, a, sizeof a)
        && vigia_mpris_normalize_spotify_id(second, b, sizeof b)) {
        return strcmp(a, b) == 0;
    }
    return strcmp(first, second) == 0;
}

static bool read_string(DBusMessageIter *value, int type, char *output, size_t capacity)
{
    if (dbus_message_iter_get_arg_type(value) != type) {
        return false;
    }
    const char *text = NULL;
    dbus_message_iter_get_basic(value, &text);
    if (text == NULL || strlen(text) >= capacity || !dbus_validate_utf8(text, NULL)) {
        return false;
    }
    memcpy(output, text, strlen(text) + 1U);
    return true;
}

static bool parse_metadata(DBusMessageIter *array, VigiaSpotifyTrack *track)
{
    if (dbus_message_iter_get_arg_type(array) != DBUS_TYPE_ARRAY
        || dbus_message_iter_get_element_type(array) != DBUS_TYPE_DICT_ENTRY) {
        return false;
    }
    VigiaSpotifyTrack fresh = { .is_playing = track->is_playing, .generation = track->generation };
    DBusMessageIter dict;
    dbus_message_iter_recurse(array, &dict);
    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry;
        DBusMessageIter value;
        dbus_message_iter_recurse(&dict, &entry);
        const char *key = NULL;
        if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_STRING) {
            return false;
        }
        dbus_message_iter_get_basic(&entry, &key);
        if (key == NULL || !dbus_message_iter_next(&entry)
            || dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_VARIANT) {
            return false;
        }
        dbus_message_iter_recurse(&entry, &value);
        char *destination = NULL;
        int type = DBUS_TYPE_STRING;
        if (strcmp(key, "xesam:title") == 0) {
            destination = fresh.title;
        } else if (strcmp(key, "xesam:url") == 0) {
            destination = fresh.uri;
        } else if (strcmp(key, "mpris:trackid") == 0) {
            destination = fresh.object_path;
            type = dbus_message_iter_get_arg_type(&value);
            if (type != DBUS_TYPE_OBJECT_PATH && type != DBUS_TYPE_STRING) {
                return false;
            }
        } else if (strcmp(key, "mpris:artUrl") == 0) {
            destination = fresh.art_url;
        } else if (strcmp(key, "xesam:artist") == 0) {
            if (dbus_message_iter_get_arg_type(&value) != DBUS_TYPE_ARRAY
                || dbus_message_iter_get_element_type(&value) != DBUS_TYPE_STRING) {
                return false;
            }
            DBusMessageIter artists;
            dbus_message_iter_recurse(&value, &artists);
            if (dbus_message_iter_get_arg_type(&artists) != DBUS_TYPE_INVALID
                && !read_string(&artists, DBUS_TYPE_STRING, fresh.artist, sizeof fresh.artist)) {
                return false;
            }
        }
        if (destination != NULL && !read_string(&value, type, destination, VIGIA_MPRIS_TEXT_CAP)) {
            return false;
        }
        if (destination == fresh.object_path && !dbus_validate_path(fresh.object_path, NULL)) {
            return false;
        }
        dbus_message_iter_next(&dict);
    }
    const char *identity = fresh.uri[0] != '\0' ? fresh.uri : fresh.object_path;
    memcpy(fresh.track_id, identity, strlen(identity) + 1U);
    *track = fresh;
    return true;
}

static bool parse_properties(DBusMessageIter *array, VigiaSpotifyTrack *track, bool complete)
{
    if (dbus_message_iter_get_arg_type(array) != DBUS_TYPE_ARRAY
        || dbus_message_iter_get_element_type(array) != DBUS_TYPE_DICT_ENTRY) {
        return false;
    }
    bool metadata_seen = false;
    bool status_seen = false;
    DBusMessageIter dict;
    dbus_message_iter_recurse(array, &dict);
    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry;
        DBusMessageIter value;
        dbus_message_iter_recurse(&dict, &entry);
        const char *key = NULL;
        if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_STRING) {
            return false;
        }
        dbus_message_iter_get_basic(&entry, &key);
        if (key == NULL || !dbus_message_iter_next(&entry)
            || dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_VARIANT) {
            return false;
        }
        dbus_message_iter_recurse(&entry, &value);
        if (strcmp(key, "PlaybackStatus") == 0) {
            char status[16] = {0};
            if (!read_string(&value, DBUS_TYPE_STRING, status, sizeof status)
                || (strcmp(status, "Playing") != 0 && strcmp(status, "Paused") != 0
                    && strcmp(status, "Stopped") != 0)) {
                return false;
            }
            track->is_playing = strcmp(status, "Playing") == 0;
            status_seen = true;
        } else if (strcmp(key, "Metadata") == 0) {
            if (!parse_metadata(&value, track)) {
                return false;
            }
            metadata_seen = true;
        }
        dbus_message_iter_next(&dict);
    }
    return !complete || (metadata_seen && status_seen);
}

static int compare_identity(const VigiaSpotifyTrack *a, const VigiaSpotifyTrack *b)
{
    if (a->uri[0] != '\0' && b->uri[0] != '\0') {
        return vigia_mpris_track_ids_equal(a->uri, b->uri) ? 0 : 1;
    }
    if (vigia_mpris_is_valid_track_id(a->object_path)
        && vigia_mpris_is_valid_track_id(b->object_path)) {
        return strcmp(a->object_path, b->object_path) == 0 ? 0 : 1;
    }
    return -1;
}

static void publish_media(VigiaMpris *player)
{
    VigiaMpris *root = player->parent != NULL ? player->parent : player;
    if (root->media_callback == NULL) {
        return;
    }
    VigiaMpris *best = root->baseline && root->owner[0] != '\0' ? root : NULL;
    for (size_t i = 0U; i < 16U; i++) {
        VigiaMpris *candidate = root->players[i];
        if (candidate == NULL || !candidate->baseline || candidate->owner[0] == '\0') {
            continue;
        }
        const int rank = candidate->current.is_playing ? 2 : (candidate->current.title[0] != '\0' ? 1 : 0);
        const int best_rank = best == NULL ? -1 : (best->current.is_playing ? 2 : (best->current.title[0] != '\0' ? 1 : 0));
        if (rank > best_rank || (rank == best_rank && strcmp(candidate->name, best->name) < 0)) {
            best = candidate;
        }
    }
    const VigiaSpotifyTrack *track = best != NULL ? &best->current : NULL;
    const VigiaSpotifyTrack *previous = &root->media_previous;
    if (root->media_published && root->media_present == (track != NULL)
        && (track == NULL || (track->is_playing == previous->is_playing
            && strcmp(track->track_id, previous->track_id) == 0
            && strcmp(track->uri, previous->uri) == 0
            && strcmp(track->object_path, previous->object_path) == 0
            && strcmp(track->title, previous->title) == 0
            && strcmp(track->artist, previous->artist) == 0
            && strcmp(track->art_url, previous->art_url) == 0))) {
        return;
    }
    root->media_published = true;
    root->media_present = track != NULL;
    if (track != NULL) {
        root->media_previous = *track;
    }
    root->media_callback(track, root->media_data);
}

static void publish(VigiaMpris *mpris, VigiaSpotifyTrack *track)
{
    bool fresh = false;
    const bool valid = vigia_mpris_is_valid_track_id(track->track_id);
    if (!mpris->baseline) {
        mpris->reference = *track;
        mpris->baseline = true;
    } else if (!valid) {
        mpris->reference = *track;
    } else {
        const bool previous_valid = vigia_mpris_is_valid_track_id(mpris->reference.track_id);
        const int comparison = compare_identity(&mpris->reference, track);
        if (previous_valid && comparison <= 0) {
            mpris->reference = *track;
        } else if (track->is_playing && track->title[0] != '\0') {
            fresh = true;
            mpris->reference = *track;
        }
    }
    track->generation = ++mpris->generation;
    mpris->current = *track;
    if (mpris->callback != NULL) {
        mpris->callback(valid ? &mpris->current : NULL, fresh, mpris->user_data);
    }
    publish_media(mpris);
}

static void cancel_pending(VigiaMpris *mpris)
{
    if (mpris->pending != NULL) {
        dbus_pending_call_cancel(mpris->pending);
        dbus_pending_call_unref(mpris->pending);
        mpris->pending = NULL;
    }
}

static void set_owner(VigiaMpris *mpris, const char *owner)
{
    if (owner == NULL || strlen(owner) >= sizeof mpris->owner
        || (owner[0] != '\0' && owner[0] != ':')) {
        return;
    }
    if (strcmp(owner, mpris->owner) == 0) {
        return;
    }
    cancel_pending(mpris);
    memcpy(mpris->owner, owner, strlen(owner) + 1U);
    mpris->owner_generation++;
    mpris->baseline = false;
    mpris->current = (VigiaSpotifyTrack){0};
    mpris->reference = (VigiaSpotifyTrack){0};
    mpris->lookup_needed = false;
    mpris->snapshot_needed = owner[0] != '\0';
    mpris->retry_at = 0.0;
    if (mpris->callback != NULL) {
        mpris->callback(NULL, false, mpris->user_data);
    }
    publish_media(mpris);
}

static void reply_received(DBusPendingCall *call, void *context)
{
    VigiaMpris *mpris = context;
    if (mpris->pending != call) {
        return;
    }
    mpris->pending = NULL;
    DBusMessage *reply = dbus_pending_call_steal_reply(call);
    const bool resolving = mpris->resolving_owner;
    bool succeeded = false;
    if (reply != NULL && dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_METHOD_RETURN
        && mpris->pending_generation == mpris->owner_generation) {
        const char *sender = dbus_message_get_sender(reply);
        DBusMessageIter value;
        if (resolving && sender != NULL && strcmp(sender, "org.freedesktop.DBus") == 0
            && dbus_message_has_signature(reply, "s") && dbus_message_iter_init(reply, &value)) {
            char owner[128] = {0};
            if (read_string(&value, DBUS_TYPE_STRING, owner, sizeof owner) && owner[0] == ':') {
                set_owner(mpris, owner);
                succeeded = true;
            }
        } else if (!resolving && sender != NULL && strcmp(sender, mpris->owner) == 0
                   && dbus_message_has_signature(reply, "a{sv}") && dbus_message_iter_init(reply, &value)) {
            VigiaSpotifyTrack track = {0};
            if (parse_properties(&value, &track, true)) {
                publish(mpris, &track);
                succeeded = true;
            }
        }
    }
    if (!succeeded) {
        const char *error = reply != NULL ? dbus_message_get_error_name(reply) : NULL;
        if (resolving) {
            mpris->lookup_needed = error == NULL || strcmp(error, DBUS_ERROR_NAME_HAS_NO_OWNER) != 0;
        } else {
            mpris->snapshot_needed = true;
        }
        mpris->retry_at = monotonic_time() + 0.25;
    }
    if (reply != NULL) {
        dbus_message_unref(reply);
    }
    dbus_pending_call_unref(call);
}

static void request_snapshot(VigiaMpris *mpris)
{
    const bool owner = mpris->lookup_needed;
    DBusMessage *request = dbus_message_new_method_call(
        owner ? "org.freedesktop.DBus" : mpris->owner,
        owner ? "/org/freedesktop/DBus" : SPOTIFY_OBJECT_PATH,
        owner ? "org.freedesktop.DBus" : "org.freedesktop.DBus.Properties",
        owner ? "GetNameOwner" : "GetAll");
    if (request == NULL) {
        mpris->retry_at = monotonic_time() + 0.25;
        return;
    }
    const char *argument = owner ? mpris->name : MPRIS_PLAYER_IFACE;
    if (!dbus_message_append_args(request, DBUS_TYPE_STRING, &argument, DBUS_TYPE_INVALID)
        || !dbus_connection_send_with_reply(mpris->conn, request, &mpris->pending, 500)
        || mpris->pending == NULL) {
        dbus_message_unref(request);
        mpris->retry_at = monotonic_time() + 0.25;
        return;
    }
    dbus_message_unref(request);
    mpris->resolving_owner = owner;
    mpris->pending_generation = mpris->owner_generation;
    if (!dbus_pending_call_set_notify(mpris->pending, reply_received, mpris, NULL)) {
        cancel_pending(mpris);
        mpris->retry_at = monotonic_time() + 0.25;
        return;
    }
    if (owner) {
        mpris->lookup_needed = false;
    } else {
        mpris->snapshot_needed = false;
    }
}

static DBusHandlerResult player_signal(DBusConnection *connection, DBusMessage *message, void *context)
{
    (void)connection;
    VigiaMpris *mpris = context;
    const char *sender = dbus_message_get_sender(message);
    if (sender == NULL) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    if (strcmp(sender, "org.freedesktop.DBus") == 0
        && dbus_message_is_signal(message, "org.freedesktop.DBus", "NameOwnerChanged")
        && dbus_message_has_signature(message, "sss")) {
        const char *name = NULL;
        const char *old_owner = NULL;
        const char *new_owner = NULL;
        if (dbus_message_get_args(message, NULL, DBUS_TYPE_STRING, &name,
            DBUS_TYPE_STRING, &old_owner, DBUS_TYPE_STRING, &new_owner, DBUS_TYPE_INVALID)
            && name != NULL && strcmp(name, mpris->name) == 0) {
            set_owner(mpris, new_owner);
        }
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    const char *path = dbus_message_get_path(message);
    if (mpris->owner[0] == '\0' || strcmp(sender, mpris->owner) != 0
        || path == NULL || strcmp(path, SPOTIFY_OBJECT_PATH) != 0
        || !dbus_message_is_signal(message, "org.freedesktop.DBus.Properties", "PropertiesChanged")
        || !dbus_message_has_signature(message, "sa{sv}as") || !mpris->baseline) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    DBusMessageIter value;
    if (!dbus_message_iter_init(message, &value)) {
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    const char *interface = NULL;
    dbus_message_iter_get_basic(&value, &interface);
    if (interface == NULL || strcmp(interface, MPRIS_PLAYER_IFACE) != 0 || !dbus_message_iter_next(&value)) {
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    VigiaSpotifyTrack track = mpris->current;
    if (parse_properties(&value, &track, false)) {
        publish(mpris, &track);
    }
    if (dbus_message_iter_next(&value)) {
        DBusMessageIter invalidated;
        dbus_message_iter_recurse(&value, &invalidated);
        while (dbus_message_iter_get_arg_type(&invalidated) == DBUS_TYPE_STRING) {
            const char *property = NULL;
            dbus_message_iter_get_basic(&invalidated, &property);
            if (property != NULL && (strcmp(property, "Metadata") == 0 || strcmp(property, "PlaybackStatus") == 0)) {
                mpris->snapshot_needed = true;
            }
            dbus_message_iter_next(&invalidated);
        }
    }
    return DBUS_HANDLER_RESULT_HANDLED;
}

static VigiaMpris *find_player(VigiaMpris *root, const char *name, bool create)
{
    if (strcmp(name, SPOTIFY_BUS_NAME) == 0) {
        return root;
    }
    const char *prefix = "org.mpris.MediaPlayer2.";
    if (root->media_callback == NULL || strncmp(name, prefix, strlen(prefix)) != 0
        || strlen(name) >= sizeof root->name || !dbus_validate_bus_name(name, NULL)) {
        return NULL;
    }
    for (size_t i = 0U; i < 16U; i++) {
        if (root->players[i] != NULL && strcmp(root->players[i]->name, name) == 0) {
            return root->players[i];
        }
    }
    if (!create) {
        return NULL;
    }
    for (size_t i = 0U; i < 16U; i++) {
        if (root->players[i] == NULL) {
            VigiaMpris *player = calloc(1U, sizeof *player);
            if (player != NULL) {
                player->parent = root;
                player->conn = root->conn;
                player->lookup_needed = true;
                memcpy(player->name, name, strlen(name) + 1U);
                root->players[i] = player;
            }
            return player;
        }
    }
    return NULL;
}

static DBusHandlerResult signal_received(DBusConnection *connection, DBusMessage *message, void *context)
{
    VigiaMpris *root = context;
    const char *sender = dbus_message_get_sender(message);
    if (sender != NULL && strcmp(sender, "org.freedesktop.DBus") == 0
        && dbus_message_is_signal(message, "org.freedesktop.DBus", "NameOwnerChanged")
        && dbus_message_has_signature(message, "sss")) {
        const char *name = NULL;
        const char *old_owner = NULL;
        const char *new_owner = NULL;
        if (dbus_message_get_args(message, NULL, DBUS_TYPE_STRING, &name,
            DBUS_TYPE_STRING, &old_owner, DBUS_TYPE_STRING, &new_owner, DBUS_TYPE_INVALID)) {
            VigiaMpris *player = find_player(root, name, new_owner[0] != '\0');
            if (player != NULL) {
                set_owner(player, new_owner);
                if (new_owner[0] == '\0' && player != root) {
                    for (size_t i = 0U; i < 16U; i++) {
                        if (root->players[i] == player) {
                            free(player);
                            root->players[i] = NULL;
                            break;
                        }
                    }
                }
            }
        }
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    DBusHandlerResult result = player_signal(connection, message, root);
    for (size_t i = 0U; i < 16U; i++) {
        if (root->players[i] != NULL
            && player_signal(connection, message, root->players[i]) == DBUS_HANDLER_RESULT_HANDLED) {
            result = DBUS_HANDLER_RESULT_HANDLED;
        }
    }
    return result;
}

static void list_received(DBusPendingCall *call, void *data)
{
    VigiaMpris *root = data;
    root->listing = NULL;
    DBusMessage *reply = dbus_pending_call_steal_reply(call);
    const char *sender = reply != NULL ? dbus_message_get_sender(reply) : NULL;
    if (reply != NULL && sender != NULL && strcmp(sender, "org.freedesktop.DBus") == 0
        && dbus_message_has_signature(reply, "as")) {
        DBusMessageIter iter;
        DBusMessageIter names;
        if (dbus_message_iter_init(reply, &iter)) {
            dbus_message_iter_recurse(&iter, &names);
            for (size_t i = 0U; i < 4096U && dbus_message_iter_get_arg_type(&names) == DBUS_TYPE_STRING; i++) {
                const char *name = NULL;
                dbus_message_iter_get_basic(&names, &name);
                (void)find_player(root, name, true);
                dbus_message_iter_next(&names);
            }
        }
    } else {
        root->list_needed = true;
        root->retry_at = monotonic_time() + 1.0;
    }
    if (reply != NULL) {
        dbus_message_unref(reply);
    }
    dbus_pending_call_unref(call);
}

static void request_list(VigiaMpris *root)
{
    DBusMessage *request = dbus_message_new_method_call("org.freedesktop.DBus",
        "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames");
    if (request == NULL) {
        root->retry_at = monotonic_time() + 1.0;
        return;
    }
    const bool sent = dbus_connection_send_with_reply(root->conn, request, &root->listing, 500);
    dbus_message_unref(request);
    if (!sent || root->listing == NULL) {
        root->retry_at = monotonic_time() + 1.0;
        return;
    }
    if (!dbus_pending_call_set_notify(root->listing, list_received, root, NULL)) {
        dbus_pending_call_cancel(root->listing);
        dbus_pending_call_unref(root->listing);
        root->listing = NULL;
        root->retry_at = monotonic_time() + 1.0;
        return;
    }
    root->list_needed = false;
}

static void clear_players(VigiaMpris *root)
{
    if (root->listing != NULL) {
        dbus_pending_call_cancel(root->listing);
        dbus_pending_call_unref(root->listing);
        root->listing = NULL;
    }
    for (size_t i = 0U; i < 16U; i++) {
        if (root->players[i] != NULL) {
            cancel_pending(root->players[i]);
            free(root->players[i]);
            root->players[i] = NULL;
        }
    }
}

void vigia_mpris_set_media_callback(VigiaMpris *mpris, VigiaMediaCallback callback, void *user_data)
{
    if (mpris != NULL) {
        if (mpris->media_callback == callback && mpris->media_data == user_data) {
            return;
        }
        mpris->media_published = false;
        if (mpris->media_callback == NULL && callback != NULL) {
            mpris->list_needed = true;
        }
        mpris->media_callback = callback;
        mpris->media_data = user_data;
        if (callback == NULL) {
            clear_players(mpris);
        }
        publish_media(mpris);
    }
}

void vigia_mpris_set_spotify_callback(VigiaMpris *mpris, VigiaSpotifyCallback callback, void *user_data)
{
    if (mpris != NULL) {
        mpris->callback = callback;
        mpris->user_data = user_data;
        mpris->reference = mpris->current;
    }
}

static void connect_bus(VigiaMpris *mpris)
{
    DBusError error;
    dbus_error_init(&error);
    mpris->conn = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
    if (mpris->conn != NULL) {
        dbus_connection_set_exit_on_disconnect(mpris->conn, false);
        if (!dbus_connection_add_filter(mpris->conn, signal_received, mpris, NULL)) {
            goto failed;
        }
        dbus_bus_add_match(mpris->conn,
            "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',member='NameOwnerChanged',arg0namespace='org.mpris.MediaPlayer2'", &error);
        if (dbus_error_is_set(&error)) {
            goto failed;
        }
        dbus_bus_add_match(mpris->conn,
            "type='signal',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged',path='/org/mpris/MediaPlayer2'", &error);
        if (dbus_error_is_set(&error)) {
            goto failed;
        }
        mpris->lookup_needed = true;
        mpris->list_needed = mpris->media_callback != NULL;
        return;
    }
failed:
    if (dbus_error_is_set(&error)) {
        dbus_error_free(&error);
    }
    if (mpris->conn != NULL) {
        dbus_connection_close(mpris->conn);
        dbus_connection_unref(mpris->conn);
        mpris->conn = NULL;
    }
    mpris->retry_at = monotonic_time() + 1.0;
}

VigiaMpris *vigia_mpris_create(VigiaSpotifyCallback callback, void *user_data)
{
    VigiaMpris *mpris = calloc(1U, sizeof *mpris);
    if (mpris != NULL) {
        mpris->callback = callback;
        mpris->user_data = user_data;
        memcpy(mpris->name, SPOTIFY_BUS_NAME, sizeof SPOTIFY_BUS_NAME);
        connect_bus(mpris);
    }
    return mpris;
}

void vigia_mpris_poll(VigiaMpris *mpris)
{
    if (mpris == NULL) {
        return;
    }
    if (mpris->conn == NULL && monotonic_time() >= mpris->retry_at) {
        connect_bus(mpris);
    }
    if (mpris->conn == NULL) {
        return;
    }
    if (!dbus_connection_read_write_dispatch(mpris->conn, 0)) {
        cancel_pending(mpris);
        clear_players(mpris);
        set_owner(mpris, "");
        dbus_connection_close(mpris->conn);
        dbus_connection_unref(mpris->conn);
        mpris->conn = NULL;
        mpris->retry_at = monotonic_time() + 1.0;
        return;
    }
    for (size_t i = 0U; i < 256U && dbus_connection_get_dispatch_status(mpris->conn) == DBUS_DISPATCH_DATA_REMAINS; i++) {
        if (dbus_connection_dispatch(mpris->conn) == DBUS_DISPATCH_NEED_MEMORY) {
            break;
        }
    }
    if (mpris->pending == NULL && monotonic_time() >= mpris->retry_at
        && (mpris->lookup_needed || (mpris->snapshot_needed && mpris->owner[0] != '\0'))) {
        request_snapshot(mpris);
    }
    if (mpris->media_callback != NULL && mpris->list_needed && mpris->listing == NULL
        && monotonic_time() >= mpris->retry_at) {
        request_list(mpris);
    }
    for (size_t i = 0U; i < 16U; i++) {
        VigiaMpris *player = mpris->players[i];
        if (player != NULL && player->pending == NULL && monotonic_time() >= player->retry_at
            && (player->lookup_needed || (player->snapshot_needed && player->owner[0] != '\0'))) {
            request_snapshot(player);
        }
    }
}

void vigia_mpris_destroy(VigiaMpris *mpris)
{
    if (mpris != NULL) {
        cancel_pending(mpris);
        clear_players(mpris);
        if (mpris->conn != NULL) {
            dbus_connection_remove_filter(mpris->conn, signal_received, mpris);
            dbus_connection_close(mpris->conn);
            dbus_connection_unref(mpris->conn);
        }
        free(mpris);
    }
}
