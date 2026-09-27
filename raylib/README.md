# Native Vigia

This directory contains Vigia's C23 shell, built with raylib's Wayland backend.
See the [main README](../README.md) for dependencies, font setup, runtime
requirements and control commands.

## Build

From the repository root:

```sh
meson setup build --prefix="$HOME/.local"
meson compile -C build
meson install -C build
```

The executables are `build/vigia` and `build/vigiactl`. Installation places them
in the prefix's `bin` directory. Wayland bindings and compiled objects stay in
the build directory.

`third_party/raylib` is based on raylib 5.5.0 at
`c1ab645ca298a2801097931d1079b10ff7eb9df8`, recorded in
`third_party/RAYLIB_REVISION`, with local Wayland changes. Dependencies and their
licences remain in `third_party/`.

## Configuration reference

The root key `version` must be the integer `1`. These optional tables accept
only the keys listed below:

| Table | Key | Default | Accepted values |
| --- | --- | --- | --- |
| `menu` | `enabled` | `true` | Boolean |
| `osd` | `enabled` | `true` | Boolean |
| `notifications` | `enabled` | `true` | Boolean |
| `launcher` | `enabled` | Whether entries exist | Boolean |
| `launcher` | `icon_size` | `48` | Integer, 24 to 96 pixels |
| `launcher` | `hover_delay_ms` | `200` | Integer, 0 to 2000 |
| `launcher` | `leave_delay_ms` | `350` | Integer, 0 to 2000 |
| `launcher` | `apps` | Empty | Up to 64 app tables |
| `music` | `enabled` | `true` | Boolean |
| `music` | `display_ms` | `5000` | Integer, 1000 to 15000 |

For example, to disable notifications while keeping the other defaults:

```toml
version = 1

[notifications]
enabled = false
```

Launcher entries use `[[launcher.apps]]`, with these required fields:

| Field | Value |
| --- | --- |
| `id` | Nonempty unique string, at most 63 bytes |
| `label` | Nonempty string, at most 127 bytes |
| `icon` | Nonempty SVG path; resolved path must fit in 4095 bytes |
| `argv` | 1 to 64 nonempty strings, each at most 4095 bytes |
| `app_ids` | 1 to 16 nonempty strings, each at most 127 bytes |

An app ID alias cannot belong to two different entries. Commands execute as
argument arrays without shell expansion. Icons are files, not icon-theme names;
relative paths are based on the config directory. The configuration file must
be valid UTF-8, contain no NUL, and be at most 256 KiB.

The config file and configured SVG icons are watched for changes. Parsing and
module preparation must succeed before a reload replaces the active state.
An invalid config at startup is reported on stderr; the menu, OSD and
notifications remain enabled, with the launcher and music popup disabled.

## Services and output

The menu displays `ext-workspace-v1` tags without activation controls. It shares
MPRIS data with the music module, but their selection rules differ: the menu
prefers a playing player, while the popup accepts only
`org.mpris.MediaPlayer2.spotify` track changes. A popup lasts `music.display_ms`.

Audio is read-only through libpulse. Notifications request the standard D-Bus
name without replacing another daemon. Disabling a module stops its unneeded
service work; it does not disable other modules that share the same service.

The shell uses one compositor-selected output, requests no keyboard focus and
reserves no screen space. It has no tray or focused-output tracking.
