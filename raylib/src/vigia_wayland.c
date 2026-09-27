#include "vigia_wayland.h"

#include "raylib.h"
#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_WAYLAND
#include "external/glfw/include/GLFW/glfw3.h"
#include "external/glfw/include/GLFW/glfw3native.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>

static GLFWcursorposfun previous_position;
static GLFWcursorenterfun previous_presence;
static GLFWmousebuttonfun previous_button;
static VigiaPointerEvent pointer;
static VigiaPointerEvent events[128];
static size_t event_start;
static size_t event_count;
static bool event_overflow;

static void queue_pointer(void)
{
    if (event_count == sizeof events / sizeof events[0]) {
        event_overflow = true;
        return;
    }
    const size_t index = (event_start + event_count) % (sizeof events / sizeof events[0]);
    events[index] = pointer;
    event_count++;
}

static int coordinate(double value)
{
    return isfinite(value) && value >= INT_MIN && value <= INT_MAX ? (int)value : -1;
}

static void pointer_position(GLFWwindow *window, double x, double y)
{
    if (previous_position != NULL) {
        previous_position(window, x, y);
    }
    pointer.x = coordinate(x);
    pointer.y = coordinate(y);
    pointer.released = false;
    queue_pointer();
}

static void pointer_presence(GLFWwindow *window, int entered)
{
    if (previous_presence != NULL) {
        previous_presence(window, entered);
    }
    pointer.present = entered == GLFW_TRUE;
    pointer.released = false;
    queue_pointer();
}

static void pointer_button(GLFWwindow *window, int button, int action, int mods)
{
    if (previous_button != NULL) {
        previous_button(window, button, action, mods);
    }
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        pointer.down = action != GLFW_RELEASE;
        pointer.released = action == GLFW_RELEASE;
        queue_pointer();
        pointer.released = false;
    }
}

bool vigia_wayland_poll_pointer(VigiaPointerEvent *event)
{
    if (event == NULL) {
        return false;
    }
    if (event_overflow) {
        event_overflow = false;
        event_count = 0U;
        event_start = 0U;
        *event = (VigiaPointerEvent){ .x = -1, .y = -1, .down = pointer.down };
        return true;
    }
    if (event_count == 0U) {
        return false;
    }
    *event = events[event_start];
    event_start = (event_start + 1U) % (sizeof events / sizeof events[0]);
    event_count--;
    return true;
}

void vigia_wayland_init_hints(void)
{
    glfwInitHint(GLFW_WAYLAND_LIBDECOR, GLFW_WAYLAND_DISABLE_LIBDECOR);
}

bool vigia_wayland_prepare(void)
{
    GLFWwindow *window = GetWindowHandle();
    if (window == NULL) {
        fputs("vigia-ray: layer-shell window has no native Wayland handle\n", stderr);
        return false;
    }
    previous_position = glfwSetCursorPosCallback(window, pointer_position);
    previous_presence = glfwSetCursorEnterCallback(window, pointer_presence);
    previous_button = glfwSetMouseButtonCallback(window, pointer_button);
    pointer = (VigiaPointerEvent){ .present = IsCursorOnScreen() };
    event_start = 0U;
    event_count = 0U;
    event_overflow = false;
    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(window, &x, &y);
    pointer.x = coordinate(x);
    pointer.y = coordinate(y);
    queue_pointer();
    return true;
}

struct wl_display *vigia_wayland_get_display(void)
{
    return IsWindowReady() ? glfwGetWaylandDisplay() : NULL;
}

struct wl_output *vigia_wayland_get_output(void)
{
    return IsWindowReady() ? glfwGetWaylandLayerOutput(GetWindowHandle()) : NULL;
}

struct wl_seat *vigia_wayland_get_seat(void)
{
    return IsWindowReady() ? glfwGetWaylandSeat() : NULL;
}
