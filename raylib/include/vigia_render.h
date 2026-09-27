#ifndef VIGIA_RENDER_H
#define VIGIA_RENDER_H

#include "raylib.h"
#include "vigia_draw.h"
#include "vigia_layout.h"
#include "vigia_model.h"

bool vigia_render_equal(const VigiaState *a, const VigiaState *b, bool notifications);

void vigia_render_content(const VigiaDraw *draw, const VigiaState *state,
                          const VigiaLayout *layout, bool notifications_enabled);

void vigia_render_frame(const VigiaDraw *draw, const VigiaState *state,
                        const VigiaLayout *layout);

#endif
