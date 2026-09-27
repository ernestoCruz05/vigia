#include "vigia_control.h"
#include "vigia_draw.h"
#include "vigia_host.h"
#include "vigia_launcher.h"
#include "vigia_layout.h"
#include "vigia_music.h"
#include "vigia_render.h"
#include "vigia_text.h"
#include "vigia_theme.h"
#include "vigia_wayland.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define VIGIA_PROBE_FRAME_COUNT 30U

static volatile sig_atomic_t stop_requested = 0;

static void request_stop(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static bool copy_text(char *out, size_t capacity, const char *text)
{
    const int written = snprintf(out, capacity, "%s", text);
    return written >= 0 && (size_t)written < capacity;
}

static bool seed_demo(VigiaState *state)
{
    if (state == NULL
        || !copy_text(state->media, sizeof state->media, "alva noto\tConversation 6")
        || !copy_text(state->clock, sizeof state->clock, "MON 07\t23:18")) {
        return false;
    }
    vigia_state_command(state, VIGIA_COMMAND_REVEAL);
    return true;
}

static bool add_demo_toast(VigiaState *state)
{
    VigiaNotification notification = {0};
    int id = 2;

    if (state == NULL) {
        return false;
    }
    if (state->active_count > 0U) {
        if (state->active[0].id == INT_MAX) {
            return false;
        }
        id = state->active[0].id + 1;
    }
    notification.id = id;
    if (!copy_text(notification.app_name, sizeof notification.app_name, "Signal")
        || !copy_text(notification.summary, sizeof notification.summary, "Ana Moura")
        || !copy_text(notification.body, sizeof notification.body, "sent you a photo")
        || !copy_text(notification.time, sizeof notification.time, "12:00")) {
        return false;
    }
    return vigia_state_push(state, &notification);
}

static void show_demo_osd(VigiaState *state)
{
    const VigiaOsd payload = {
        .label = "VOL", .message = "62", .value = 62, .has_fill = true,
    };
    (void)vigia_state_show_osd(state, &payload);
}

static void apply_command(VigiaHost *host, VigiaCommand command)
{
    if (command == VIGIA_COMMAND_DEMO_TOAST) {
        if (!add_demo_toast(&host->state)) {
            fputs("vigia: could not add mock notification\n", stderr);
        }
    } else if (command == VIGIA_COMMAND_DEMO_OSD) {
        show_demo_osd(&host->state);
    } else {
        vigia_host_command(host, command);
    }
}

static void receive_commands(const VigiaControl *control, VigiaHost *host)
{
    for (size_t index = 0U; index < 32U; index++) {
        VigiaCommand command = VIGIA_COMMAND_TOGGLE;
        if (!vigia_control_receive(control, &command)) {
            break;
        }
        apply_command(host, command);
    }
}

static bool monotonic_time(double *seconds)
{
    struct timespec now = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return false;
    }
    *seconds = (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
    return true;
}

static bool regions_equal(const Rectangle *a, size_t a_count, const Rectangle *b, size_t b_count)
{
    if (a_count != b_count) {
        return false;
    }
    for (size_t i = 0U; i < a_count; i++) {
        if (a[i].x != b[i].x || a[i].y != b[i].y
            || a[i].width != b[i].width || a[i].height != b[i].height) {
            return false;
        }
    }
    return true;
}

static int run(bool demo, bool probe, const char *config_path)
{
    const char *const runtime_dir = getenv("XDG_RUNTIME_DIR");
    VigiaControl control = { .fd = -1 };
    VigiaDraw draw = {0};
    VigiaHost *host = NULL;
    bool window_initialized = false;
    int result = EXIT_FAILURE;
    size_t frames = 0U;

    if (runtime_dir == NULL || runtime_dir[0] == '\0') {
        fputs("vigia: XDG_RUNTIME_DIR is unset\n", stderr);
        goto out;
    }
    vigia_wayland_init_hints();
    SetConfigFlags(FLAG_WINDOW_LAYER_SHELL | FLAG_WINDOW_TRANSPARENT
                   | FLAG_WINDOW_UNFOCUSED);
    InitWindow(0, 0, "vigia-ray");
    window_initialized = IsWindowReady();
    if (!window_initialized || !vigia_wayland_prepare()) {
        fputs("vigia: Wayland layer-shell initialization failed\n", stderr);
        goto out;
    }
    SetTargetFPS(0);
    if (!vigia_control_open(&control, runtime_dir)) {
        goto out;
    }
    if (!vigia_draw_init(&draw)) {
        fputs("vigia: draw initialization failed\n", stderr);
        goto out;
    }

    host = vigia_host_create(&draw);
    if (host == NULL) {
        goto out;
    }
    (void)vigia_host_register(host, vigia_menu_module_create());
    (void)vigia_host_register(host, vigia_osd_module_create());
    (void)vigia_host_register(host, vigia_notify_module_create());
    (void)vigia_host_register(host, vigia_launcher_module_create());
    (void)vigia_host_register(host, vigia_music_module_create());
    host->demo = demo;
    if (!vigia_host_init(host, config_path)) {
        goto out;
    }

    if (demo && !seed_demo(&host->state)) {
        fputs("vigia: could not seed demo state\n", stderr);
        goto out;
    }

    double previous_tick = 0.0;
    if (!monotonic_time(&previous_tick)) {
        goto out;
    }
    Rectangle previous_regions[VIGIA_LAYOUT_MAX_INPUT_REGIONS] = {0};
    size_t previous_region_count = 0U;
    bool regions_valid = false;
    while (!stop_requested && !WindowShouldClose()
           && (!probe || frames < VIGIA_PROBE_FRAME_COUNT)) {
        double tick = 0.0;
        if (!monotonic_time(&tick)) {
            goto out;
        }
        const float elapsed = (float)(tick - previous_tick);
        previous_tick = tick;
        vigia_host_check_reload(host);
        VigiaPointerEvent pointer_event = {0};
        while (vigia_wayland_poll_pointer(&pointer_event)) {
            vigia_host_handle_pointer(host,
                pointer_event.present ? pointer_event.x : -1,
                pointer_event.present ? pointer_event.y : -1,
                pointer_event.down, pointer_event.released);
        }
        vigia_host_update(host, elapsed);
        receive_commands(&control, host);
        Rectangle raylib_regions[VIGIA_LAYOUT_MAX_INPUT_REGIONS] = {0};
        const size_t region_count = vigia_host_collect_input_regions(
            host, raylib_regions, VIGIA_LAYOUT_MAX_INPUT_REGIONS);
        const bool resized = IsWindowResized();
        if (!regions_valid || resized || !regions_equal(raylib_regions, region_count,
                previous_regions, previous_region_count)) {
            SetWindowLayerShellInputRegions(raylib_regions, (int)region_count);
            memcpy(previous_regions, raylib_regions, region_count * sizeof raylib_regions[0]);
            previous_region_count = region_count;
            regions_valid = true;
        }
        const bool changed = vigia_host_needs_redraw(host);
        const bool drawing = changed || resized;
        if (drawing) {
            vigia_host_render(host);
            EndDrawing();
        } else {
            PollInputEvents();
        }
        double completed = 0.0;
        if (!monotonic_time(&completed)) {
            goto out;
        }
        const double remaining = 1.0 / 60.0 - (completed - tick);
        if (remaining > 0.0) {
            struct timespec delay = {.tv_nsec = (long)(remaining * 1000000000.0)};
            while (nanosleep(&delay, &delay) != 0) {
                if (errno != EINTR) {
                    goto out;
                }
                if (stop_requested) {
                    break;
                }
            }
        }
        frames++;
    }

    result = EXIT_SUCCESS;
out:
    if (host != NULL) {
        vigia_host_destroy(host);
    }
    vigia_control_close(&control);
    vigia_draw_shutdown(&draw);
    if (window_initialized) {
        CloseWindow();
    }
    return result;
}

int main(int argc, char *argv[])
{
    bool demo = false;
    bool probe = false;
    const char *config_path = NULL;

    for (int index = 1; index < argc; index++) {
        if (strcmp(argv[index], "--demo") == 0) {
            demo = true;
        } else if (strcmp(argv[index], "--probe") == 0) {
            probe = true;
        } else if (strcmp(argv[index], "--config") == 0 && index + 1 < argc) {
            config_path = argv[++index];
        } else {
            fprintf(stderr, "usage: %s [--demo] [--probe] [--config PATH]\n", argv[0]);
            return EXIT_FAILURE;
        }
    }
    struct sigaction action = { .sa_handler = request_stop };
    if (sigemptyset(&action.sa_mask) != 0
        || sigaction(SIGTERM, &action, NULL) != 0
        || sigaction(SIGINT, &action, NULL) != 0) {
        fputs("vigia: could not install shutdown handlers\n", stderr);
        return EXIT_FAILURE;
    }
    return run(demo, probe, config_path);
}
