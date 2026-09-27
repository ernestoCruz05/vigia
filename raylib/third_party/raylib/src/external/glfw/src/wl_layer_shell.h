//========================================================================
// GLFW layer-shell configure helpers for the Vigia Wayland extension.
//========================================================================

#ifndef VIGIA_WL_LAYER_SHELL_H
#define VIGIA_WL_LAYER_SHELL_H

#include <limits.h>
#include <stdint.h>

typedef enum VigiaLayerConfigureResult
{
    VIGIA_LAYER_CONFIGURE_OK,
    VIGIA_LAYER_CONFIGURE_FALLBACK_INVALID,
    VIGIA_LAYER_CONFIGURE_DIMENSION_INVALID
} VigiaLayerConfigureResult;

static inline VigiaLayerConfigureResult vigiaResolveLayerSurfaceConfigure(
    uint32_t configuredWidth, uint32_t configuredHeight,
    int fallbackWidth, int fallbackHeight,
    int* width, int* height)
{
    if (!width || !height)
        return VIGIA_LAYER_CONFIGURE_DIMENSION_INVALID;

    if ((configuredWidth == 0U || configuredHeight == 0U) &&
        (fallbackWidth <= 0 || fallbackHeight <= 0))
    {
        return VIGIA_LAYER_CONFIGURE_FALLBACK_INVALID;
    }

    const uint32_t resolvedWidth = configuredWidth == 0U
        ? (uint32_t) fallbackWidth : configuredWidth;
    const uint32_t resolvedHeight = configuredHeight == 0U
        ? (uint32_t) fallbackHeight : configuredHeight;

    if (resolvedWidth == 0U || resolvedHeight == 0U ||
        resolvedWidth > (uint32_t) INT_MAX ||
        resolvedHeight > (uint32_t) INT_MAX)
    {
        return VIGIA_LAYER_CONFIGURE_DIMENSION_INVALID;
    }

    *width = (int) resolvedWidth;
    *height = (int) resolvedHeight;
    return VIGIA_LAYER_CONFIGURE_OK;
}

#endif
