#include "vigia_model.h"

#include <math.h>

static size_t bounded_count(size_t count)
{
    return count < VIGIA_WORKSPACE_CAP ? count : VIGIA_WORKSPACE_CAP;
}

const VigiaTagVisual *vigia_state_tag_visual(const VigiaState *state, int workspace_id)
{
    if (state != NULL) {
        for (size_t i = 0U; i < bounded_count(state->tag_visual_count); i++) {
            if (state->tag_visuals[i].workspace_id == workspace_id) {
                return &state->tag_visuals[i];
            }
        }
    }
    return NULL;
}


static unsigned int interpolate_color(unsigned int from, unsigned int target, float t)
{
    if (t >= 1.0F) {
        return target;
    }
    unsigned int rgba = 0U;
    for (unsigned int shift = 0U; shift < 32U; shift += 8U) {
        const float a = (float)((from >> shift) & 0xFFU);
        const float b = (float)((target >> shift) & 0xFFU);
        const unsigned int channel = (unsigned int)roundf(fminf(255.0F,
            fmaxf(0.0F, a + (b - a) * t)));
        rgba |= channel << shift;
    }
    return rgba;
}


static void update_visual(VigiaTagVisual *visual, float target_height,
                           unsigned int target_rgba, float seconds)
{
    if (visual->height_target != target_height) {
        visual->height_from = visual->height;
        visual->height_target = target_height;
        visual->height_elapsed = 0.0F;
    }
    if (visual->rgba_target != target_rgba) {
        visual->rgba_from = visual->rgba;
        visual->rgba_target = target_rgba;
        visual->color_elapsed = 0.0F;
    }
    const float duration = VIGIA_MENU_TAG_SWAP_SECONDS;

    const float delta = fminf(seconds, duration);
    visual->height_elapsed = fminf(visual->height_elapsed + delta, duration);
    visual->color_elapsed = fminf(visual->color_elapsed + delta, duration);
    const float t = visual->height_elapsed / duration;
    const float remaining = 1.0F - t;
    const float eased = 1.0F - remaining * remaining * remaining;
    visual->height = t >= 1.0F ? target_height
        : visual->height_from + (target_height - visual->height_from) * eased;
    visual->rgba = interpolate_color(visual->rgba_from, target_rgba,
                                     visual->color_elapsed / duration);
}

void vigia_state_update_tags(VigiaState *state, float seconds)
{
    if (state == NULL || !isfinite(seconds) || seconds < 0.0F) {
        return;
    }
    VigiaTagVisual next[VIGIA_WORKSPACE_CAP] = {0};
    size_t count = 0U;
    for (size_t i = 0U; i < bounded_count(state->workspace_count); i++) {
        const int id = state->workspace_ids[i];
        bool duplicate = false;
        for (size_t j = 0U; j < count; j++) {
            if (next[j].workspace_id == id) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        const bool active = id == state->active_workspace;
        const float height = active ? (float)VIGIA_MENU_SPINE_ACTIVE_HEIGHT
                                    : (float)VIGIA_MENU_SPINE_HEIGHT;
        const unsigned int rgba = state->workspace_urgent[i] ? VIGIA_WARNING_RGBA
            : (active ? VIGIA_FOREGROUND_RGBA : VIGIA_DIM_RGBA);
        const VigiaTagVisual *old = vigia_state_tag_visual(state, id);
        if (old != NULL) {
            next[count] = *old;
            update_visual(&next[count], height, rgba, seconds);
        } else {
            next[count] = (VigiaTagVisual){ .workspace_id = id,
                .height = height, .height_from = height, .height_target = height,
                .rgba = rgba, .rgba_from = rgba, .rgba_target = rgba };
        }
        count++;
    }
    for (size_t i = 0U; i < VIGIA_WORKSPACE_CAP; i++) {
        state->tag_visuals[i] = next[i];
    }
    state->tag_visual_count = count;
}

bool vigia_state_set_active_workspace(VigiaState *state, int workspace_id)
{
    if (state != NULL) {
        for (size_t i = 0U; i < bounded_count(state->workspace_count); i++) {
            if (state->workspace_ids[i] == workspace_id) {
                state->active_workspace = workspace_id;
                vigia_state_update_tags(state, 0.0F);
                return true;
            }
        }
    }
    return false;
}

bool vigia_state_set_workspace_urgent(VigiaState *state, int workspace_id, bool urgent)
{
    if (state != NULL) {
        for (size_t i = 0U; i < bounded_count(state->workspace_count); i++) {
            if (state->workspace_ids[i] == workspace_id) {
                state->workspace_urgent[i] = urgent;
                vigia_state_update_tags(state, 0.0F);
                return true;
            }
        }
    }
    return false;
}
