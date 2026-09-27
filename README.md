# Vigia

Vigia is a native C/raylib shell for the Leme Wayland compositor. It has a
floating menu with workspace tags, a media line and a clock, plus notifications,
a volume OSD, an app launcher and a brief Spotify track-change popup.

## Build and run

You need a C23 compiler, Meson 1.3 or newer, Ninja, `pkg-config`, `wayland-scanner`,
and development packages providing:

```text
wayland-client wayland-egl egl xkbcommon gl
dbus-1 libpulse libcurl librsvg-2.0 cairo fontconfig
```

Patched raylib/GLFW and the TOML parser are vendored; the build downloads nothing. The native runtime does not use Qt.
Cairo and librsvg are used to rasterize launcher SVG icons.

Build from the repository root:

```sh
meson setup build --prefix="$HOME/.local"
meson compile -C build
meson install -C build
```

To uninstall, run `ninja -C build uninstall` using the same prefix. Packaging can
stage installation with `DESTDIR=/path/to/staging meson install -C build`.

Run inside a Wayland session with layer-shell support, a valid
`XDG_RUNTIME_DIR`, and `WAYLAND_DISPLAY`. Set `VIGIA_FONT` to your installed
Deserted 8x16 BDF file. The font is not bundled; an unset font or the wrong
strike size prevents startup.

```sh
export VIGIA_FONT="$HOME/.local/share/fonts/Deserted/deserted-normal-normal-normal-8x16.bdf"
vigia
```

A session D-Bus is needed for notifications and MPRIS. Audio observation uses
the PulseAudio client API, including PipeWire's PulseAudio compatibility
server. Workspace tags need `ext-workspace-v1`; launcher window matching and
activation need `wlr-foreign-toplevel-management-unstable-v1`.

## Controls

From another terminal in the same session:

```sh
vigiactl toggle
vigiactl reveal
vigiactl dismiss
vigiactl recall-toggle
vigiactl recall-dismiss
vigiactl launcher-toggle
vigiactl launcher-dismiss
```

Put the install prefix's `bin` directory on your session's `PATH`. Start `vigia`
from your compositor's startup configuration with `VIGIA_FONT` set. Bind your
menu key to `vigiactl toggle`, your launcher key to `vigiactl launcher-toggle`,
and your notification-history key to `vigiactl recall-toggle`. The client sends commands through
`$XDG_RUNTIME_DIR/vigia-ray.sock`; the shell must already be running.

The menu does not take keyboard focus or reserve desktop space. Workspace tags
are display-only: clicking a tag does not switch workspaces. The clock uses
local time. Volume and mute changes are observed, never set by Vigia; keep
using your existing audio keybindings. The OSD shows briefly on a change and
uses the menu's centre while the menu is open.

The media line can show metadata from MPRIS players, including browsers. The
music popup listens only to the exact bus name `org.mpris.MediaPlayer2.spotify`.
It appears for a qualifying track change while playing, not for the initial
snapshot or browser/YouTube events. Vigia has no playback controls.

Notifications appear as toasts and can be reviewed in recall. Vigia requests
`org.freedesktop.Notifications` without replacing its current owner. If another
notification daemon is running, it keeps the name; Vigia retries after the name
becomes free. Running Vigia does not stop that daemon or reconfigure your session.

## Configuration

Vigia reads `$XDG_CONFIG_HOME/vigia/config.toml`, falling back to
`$HOME/.config/vigia/config.toml`. You can select a file explicitly:

```sh
vigia --config ./config.toml
```

Without a config file, the menu, OSD, notifications and music popup are enabled.
The launcher has no entries and stays disabled. A config file must contain
`version = 1`. Unknown keys are rejected.

This example adds a terminal to the launcher. Install `foot` and place a valid
SVG at `icons/terminal.svg` beside the config file, or change those values for
your application:

```toml
version = 1

[[launcher.apps]]
id = "terminal"
label = "Terminal"
icon = "icons/terminal.svg"
argv = ["foot"]
app_ids = ["foot"]
```

Each entry needs all five fields. `argv` is an argument array, not a shell
command; `app_ids` matches the application's reported Wayland app ID. Relative
icon paths resolve against the config directory. The launcher enables itself
when entries exist unless `[launcher].enabled` is explicitly false.

Configuration and launcher SVG changes reload while Vigia is running. A rejected
reload keeps the previous configuration. New C modules require a rebuild and
restart. See [the native configuration reference](raylib/README.md) for options
and limits.

## Current limits

Vigia uses one layer-shell surface on a compositor-selected output. It does not
follow the focused output or create a shell per monitor. There is no system tray,
network or battery panel.

For a deterministic preview, start with `vigia --demo`, then use
`demo-osd` or `demo-toast` with `vigiactl` to show those surfaces. Demo mode
uses sample data.
