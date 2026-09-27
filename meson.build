project('vigia', 'c', meson_version: '>=1.3.0', default_options: ['c_std=c23', 'buildtype=debug', 'optimization=g', 'warning_level=0'])

cc = meson.get_compiler('c')
scanner = find_program('wayland-scanner')
wayland_deps = [dependency('wayland-client'), dependency('wayland-egl'), dependency('egl'), dependency('xkbcommon'), dependency('gl'), dependency('threads'), cc.find_library('m'), cc.find_library('dl'), cc.find_library('rt')]
runtime_deps = wayland_deps + [dependency('dbus-1'), dependency('libpulse'), dependency('libcurl'), dependency('librsvg-2.0'), dependency('cairo'), dependency('fontconfig')]
strict_args = ['-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-Wsign-conversion', '-Wshadow', '-Wvla', '-Wcast-qual', '-Wpointer-arith', '-Wmissing-prototypes', '-Wdouble-promotion', '-Wwrite-strings', '-Werror', '-D_DEFAULT_SOURCE', '-D_POSIX_C_SOURCE=200809L']
if get_option('b_sanitize') != 'none'
  add_project_arguments('-fno-sanitize-recover=all', '-fno-omit-frame-pointer', language: 'c')
  add_project_link_arguments('-fno-sanitize-recover=all', language: 'c')
endif

subdir('raylib/protocols')
subdir('raylib/third_party/raylib')
toml = static_library('tomlc99', 'raylib/third_party/tomlc99/toml.c', override_options: ['c_std=c11', 'optimization=2'], c_args: ['-Wall', '-Wextra'], install: false)

native_inc = include_directories('raylib/include', 'raylib/third_party/tomlc99')
raylib_inc = include_directories('raylib/third_party/raylib/src', is_system: true)
native_sources = files(
  'raylib/src/main.c',
  'raylib/src/vigia_control.c',
  'raylib/src/vigia_wayland.c',
  'raylib/src/vigia_draw.c',
  'raylib/src/vigia_layout.c',
  'raylib/src/vigia_model.c',
  'raylib/src/vigia_tags.c',
  'raylib/src/vigia_osd.c',
  'raylib/src/vigia_render.c',
  'raylib/src/vigia_text.c',
  'raylib/src/vigia_menu_text.c',
  'raylib/src/vigia_menu_render.c',
  'raylib/src/vigia_host.c',
  'raylib/src/vigia_config.c',
  'raylib/src/vigia_watcher.c',
  'raylib/src/vigia_notifications.c',
  'raylib/src/vigia_audio.c',
  'raylib/src/vigia_clock.c',
  'raylib/src/vigia_workspace.c',
  'raylib/src/vigia_image.c',
  'raylib/src/vigia_mpris.c',
  'raylib/src/vigia_art.c',
  'raylib/src/vigia_icon.c',
  'raylib/src/vigia_window.c',
  'raylib/src/modules/menu/vigia_menu_module.c',
  'raylib/src/modules/osd/vigia_osd_module.c',
  'raylib/src/modules/notifications/vigia_notify_module.c',
  'raylib/src/modules/launcher/vigia_launcher.c',
  'raylib/src/modules/music/vigia_music.c',
)
executable('vigia', native_sources, native_protocols, include_directories: [native_inc, raylib_inc, protocol_inc], c_args: strict_args, dependencies: runtime_deps, link_with: [raylib, toml], override_options: ['b_asneeded=false'], install: true)
executable('vigiactl', ['raylib/src/control_cli.c', 'raylib/src/vigia_control.c'], include_directories: native_inc, c_args: strict_args, install: true)

install_data(
  'raylib/third_party/raylib/LICENSE',
  'raylib/third_party/raylib/src/external/glfw/LICENSE.md',
  'raylib/third_party/tomlc99/LICENSE',
  rename: ['raylib.txt', 'glfw.txt', 'tomlc99.txt'],
  install_dir: get_option('datadir') / 'licenses' / 'vigia',
)
