#include "vigia_model.h"

#include <math.h>

void vigia_reveal_begin(VigiaReveal *reveal, bool opening)
{
    if (reveal == NULL) {
        return;
    }
    reveal->from = reveal->value;
    reveal->target = opening ? 1.0F : 0.0F;
    reveal->duration = (opening ? VIGIA_MENU_OPEN_SECONDS
                                : VIGIA_MENU_CLOSE_SECONDS)
        * fabsf(reveal->target - reveal->from);
    reveal->elapsed = 0.0F;
}

void vigia_reveal_update(VigiaReveal *reveal, float seconds)
{
    if (reveal == NULL) {
        return;
    }
    reveal->elapsed += seconds;
    const float t = (reveal->duration <= 0.0F)
        ? 1.0F
        : fminf(reveal->elapsed / reveal->duration, 1.0F);

    const float eased = (reveal->target > reveal->from)
        ? 1.0F - powf(1.0F - t, 3.0F)
        : powf(t, 3.0F);
    reveal->value = reveal->from + (reveal->target - reveal->from) * eased;
}

static void vigia_state_remember(VigiaState *state,
                                 const VigiaNotification *item)
{
    if (item->transient) {
        return;
    }

    const size_t movable = state->history_count < VIGIA_MAX_HISTORY
        ? state->history_count
        : VIGIA_MAX_HISTORY - 1U;
    for (size_t index = movable; index > 0; index--) {
        state->history[index] = state->history[index - 1U];
    }

    state->history[0] = *item;
    if (state->history_count < VIGIA_MAX_HISTORY) {
        state->history_count++;
    }
}

static size_t vigia_state_find(int id, const VigiaNotification *items,
                               size_t count)
{
    for (size_t index = 0; index < count; index++) {
        if (items[index].id == id) {
            return index;
        }
    }

    return count;
}

static void vigia_state_remove(VigiaNotification *items, size_t *count,
                               size_t index)
{
    if (index >= *count) {
        return;
    }

    for (size_t current = index; current + 1U < *count; current++) {
        items[current] = items[current + 1U];
    }
    (*count)--;
}

bool vigia_hit_equal(VigiaHit first, VigiaHit second)
{
    return first.kind == second.kind && first.index == second.index;
}

void vigia_state_point(VigiaState *state, VigiaHit hit, bool down)
{
    if (state == NULL) {
        return;
    }
    state->hovered = hit;
    if (!down) {
        state->pressed = (VigiaHit){ .kind = VIGIA_HIT_NONE, .index = 0U };
        return;
    }


    if (state->pressed.kind == VIGIA_HIT_NONE) {
        state->pressed = hit;
    } else if (!vigia_hit_equal(state->pressed, hit)) {
        state->pressed = (VigiaHit){ .kind = VIGIA_HIT_NONE, .index = 0U };
    }
}

void vigia_state_init(VigiaState *state)
{
    if (state == NULL) {
        return;
    }

    *state = (VigiaState){0};
    state->workspace_ids[0] = 1;
    state->workspace_ids[1] = 2;
    state->workspace_ids[2] = 3;
    state->workspace_ids[3] = 8;
    state->workspace_ids[4] = 9;
    state->workspace_count = 5;
    state->active_workspace = 2;
    vigia_state_update_tags(state, 0.0F);
}

void vigia_state_command(VigiaState *state, VigiaCommand command)
{
    if (state == NULL) {
        return;
    }

    switch (command) {
    case VIGIA_COMMAND_TOGGLE:
        state->phase = state->phase == VIGIA_REVEALING
                || state->phase == VIGIA_REVEALED
            ? VIGIA_DISMISSING
            : VIGIA_REVEALING;
        vigia_reveal_begin(&state->reveal, state->phase == VIGIA_REVEALING);
        break;
    case VIGIA_COMMAND_REVEAL:
        state->phase = VIGIA_REVEALING;
        vigia_reveal_begin(&state->reveal, true);
        break;
    case VIGIA_COMMAND_DISMISS:
        state->phase = VIGIA_DISMISSING;
        vigia_reveal_begin(&state->reveal, false);
        break;
    case VIGIA_COMMAND_RECALL_TOGGLE:
        state->recalling = !state->recalling;
        break;
    case VIGIA_COMMAND_RECALL_DISMISS:
        state->recalling = false;
        break;
    case VIGIA_COMMAND_LAUNCHER_TOGGLE:
    case VIGIA_COMMAND_LAUNCHER_DISMISS:
    case VIGIA_COMMAND_DEMO_TOAST:
    case VIGIA_COMMAND_DEMO_OSD:
        break;
    }
}

void vigia_state_update(VigiaState *state, float seconds)
{
    if (state == NULL || !isfinite(seconds) || seconds < 0.0F) {
        return;
    }
    vigia_state_update_tags(state, seconds);


    const float borrowed = state->phase == VIGIA_DISMISSING && state->reveal.value > 0.0F
        ? fminf(seconds, fmaxf(0.0F, state->reveal.duration - state->reveal.elapsed)) : 0.0F;
    if (borrowed > 0.0F) {
        vigia_state_update_osd(state, borrowed);
    }



    if (state->phase == VIGIA_REVEALING) {
        vigia_reveal_update(&state->reveal, seconds);
        if (state->reveal.value >= 1.0F) {
            state->reveal.value = 1.0F;
            state->phase = VIGIA_REVEALED;
        }
    } else if (state->phase == VIGIA_DISMISSING) {
        vigia_reveal_update(&state->reveal, seconds);
        if (state->reveal.value <= 0.0F) {
            state->reveal.value = 0.0F;
            state->phase = VIGIA_HIDDEN;
        }
    }
    vigia_state_update_osd(state, seconds - borrowed);
}

bool vigia_state_push(VigiaState *state, const VigiaNotification *item)
{
    if (state == NULL || item == NULL) {
        return false;
    }

    const size_t replacement = vigia_state_find(item->id, state->active,
                                                state->active_count);
    if (replacement < state->active_count) {
        state->active[replacement] = *item;
        return true;
    }

    if (state->active_count >= VIGIA_MAX_ACTIVE) {
        vigia_state_remember(state, item);
        return false;
    }

    for (size_t index = state->active_count; index > 0; index--) {
        state->active[index] = state->active[index - 1U];
    }
    state->active[0] = *item;
    state->active_count++;
    return true;
}

void vigia_state_expire(VigiaState *state, int id)
{
    if (state == NULL) {
        return;
    }

    const size_t index = vigia_state_find(id, state->active,
                                          state->active_count);
    if (index == state->active_count) {
        return;
    }

    vigia_state_remember(state, &state->active[index]);
    vigia_state_remove(state->active, &state->active_count, index);
}

void vigia_state_dismiss(VigiaState *state, int id)
{
    if (state == NULL) {
        return;
    }

    const size_t active_index = vigia_state_find(id, state->active,
                                                 state->active_count);
    vigia_state_remove(state->active, &state->active_count, active_index);

    const size_t history_index = vigia_state_find(id, state->history,
                                                  state->history_count);
    vigia_state_remove(state->history, &state->history_count, history_index);
}

void vigia_state_clear_history(VigiaState *state)
{
    if (state == NULL) {
        return;
    }

    state->history_count = 0;
}
