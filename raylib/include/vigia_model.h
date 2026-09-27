#ifndef VIGIA_MODEL_H
#define VIGIA_MODEL_H

#include "vigia_theme.h"

#include <stdbool.h>
#include <stddef.h>

#define VIGIA_MAX_ACTIVE 10
#define VIGIA_MAX_HISTORY 20
#define VIGIA_TEXT_CAP 256
#define VIGIA_APP_CAP 64
#define VIGIA_WORKSPACE_CAP 16

typedef enum {
    VIGIA_HIDDEN,
    VIGIA_REVEALING,
    VIGIA_REVEALED,
    VIGIA_DISMISSING
} VigiaPhase;

typedef enum {
    VIGIA_COMMAND_TOGGLE,
    VIGIA_COMMAND_REVEAL,
    VIGIA_COMMAND_DISMISS,
    VIGIA_COMMAND_RECALL_TOGGLE,
    VIGIA_COMMAND_RECALL_DISMISS,
    VIGIA_COMMAND_DEMO_TOAST,
    VIGIA_COMMAND_DEMO_OSD,
    VIGIA_COMMAND_LAUNCHER_TOGGLE,
    VIGIA_COMMAND_LAUNCHER_DISMISS
} VigiaCommand;

typedef struct {
    int id;
    char app_name[VIGIA_APP_CAP];
    char summary[VIGIA_TEXT_CAP];
    char body[VIGIA_TEXT_CAP];
    char time[6];
    bool critical;
    bool transient;
} VigiaNotification;

typedef struct {
    char label[8];
    char message[VIGIA_TEXT_CAP];
    int value;
    bool visible;
    bool has_fill;
    bool warning;
} VigiaOsd;

typedef enum {
    VIGIA_HIT_NONE,
    VIGIA_HIT_TAG,
    VIGIA_HIT_RECALL_ROW,
    VIGIA_HIT_CLEAR_ALL
} VigiaHitKind;

typedef struct {
    VigiaHitKind kind;
    size_t index;
} VigiaHit;

typedef struct {
    float value;
    float from;
    float target;
    float elapsed;
    float duration;
} VigiaReveal;

void vigia_reveal_begin(VigiaReveal *reveal, bool opening);
void vigia_reveal_update(VigiaReveal *reveal, float seconds);

typedef struct {
    int workspace_id;
    float height;
    float height_from;
    float height_target;
    float height_elapsed;
    unsigned int rgba;
    unsigned int rgba_from;
    unsigned int rgba_target;
    float color_elapsed;
} VigiaTagVisual;

typedef struct {
    VigiaPhase phase;
    VigiaReveal reveal;
    bool recalling;
    int workspace_ids[VIGIA_WORKSPACE_CAP];
    size_t workspace_count;
    int active_workspace;
    bool workspace_urgent[VIGIA_WORKSPACE_CAP];
    VigiaTagVisual tag_visuals[VIGIA_WORKSPACE_CAP];
    size_t tag_visual_count;
    char media[VIGIA_TEXT_CAP];
    char clock[32];
    VigiaNotification active[VIGIA_MAX_ACTIVE];
    size_t active_count;
    VigiaNotification history[VIGIA_MAX_HISTORY];
    size_t history_count;
    VigiaOsd osd;
    float osd_hold_remaining;
    VigiaReveal osd_pill;
    VigiaHit hovered;
    VigiaHit pressed;
} VigiaState;

bool vigia_state_show_osd(VigiaState *state, const VigiaOsd *payload);
void vigia_state_update_osd(VigiaState *state, float seconds);
void vigia_state_update_tags(VigiaState *state, float seconds);
bool vigia_state_set_active_workspace(VigiaState *state, int workspace_id);
bool vigia_state_set_workspace_urgent(VigiaState *state, int workspace_id, bool urgent);
const VigiaTagVisual *vigia_state_tag_visual(const VigiaState *state, int workspace_id);

bool vigia_hit_equal(VigiaHit first, VigiaHit second);
void vigia_state_init(VigiaState *state);
void vigia_state_point(VigiaState *state, VigiaHit hit, bool down);
void vigia_state_command(VigiaState *state, VigiaCommand command);
void vigia_state_update(VigiaState *state, float seconds);
bool vigia_state_push(VigiaState *state, const VigiaNotification *item);
void vigia_state_expire(VigiaState *state, int id);
void vigia_state_dismiss(VigiaState *state, int id);
void vigia_state_clear_history(VigiaState *state);

#endif
