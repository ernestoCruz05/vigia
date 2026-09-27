#include "vigia_model.h"

#include <math.h>
#include <string.h>

#define OSD_HOLD_SECONDS 1.5F

static void update_pill(VigiaState *state, float seconds)
{
    if (state->reveal.value > 0.0F) {

        state->osd_pill = (VigiaReveal){0};
        return;
    }
    const float target = state->osd.visible ? 1.0F : 0.0F;
    if (state->osd_pill.target != target) {
        vigia_reveal_begin(&state->osd_pill, state->osd.visible);
    }
    if (state->osd_pill.value != target) {
        vigia_reveal_update(&state->osd_pill,
                            fminf(seconds, state->osd_pill.duration));
    }
}

bool vigia_state_show_osd(VigiaState *state, const VigiaOsd *payload)
{
    if (state == NULL || payload == NULL
        || memchr(payload->label, '\0', sizeof payload->label) == NULL
        || memchr(payload->message, '\0', sizeof payload->message) == NULL) {
        return false;
    }
    state->osd = *payload;
    state->osd.visible = true;
    state->osd.value = state->osd.value < 0 ? 0
        : (state->osd.value > 100 ? 100 : state->osd.value);
    state->osd_hold_remaining = OSD_HOLD_SECONDS;
    update_pill(state, 0.0F);
    return true;
}

void vigia_state_update_osd(VigiaState *state, float seconds)
{
    if (state == NULL || !isfinite(seconds) || seconds < 0.0F) {
        return;
    }
    if (!state->osd.visible) {
        update_pill(state, seconds);
        return;
    }
    const float held = fminf(seconds, fmaxf(0.0F, state->osd_hold_remaining));
    update_pill(state, held);
    state->osd_hold_remaining -= held;
    if (state->osd_hold_remaining <= 0.0F) {
        state->osd_hold_remaining = 0.0F;
        state->osd = (VigiaOsd){0};

        update_pill(state, seconds - held);
    }
}
