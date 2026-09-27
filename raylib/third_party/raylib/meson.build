glfw_protocols = []
foreach name : ['wayland', 'xdg-shell', 'xdg-decoration-unstable-v1', 'viewporter', 'relative-pointer-unstable-v1', 'pointer-constraints-unstable-v1', 'fractional-scale-v1', 'xdg-activation-v1', 'idle-inhibit-unstable-v1']
  foreach kind : ['client-header', 'private-code']
    suffix = kind == 'client-header' ? '.h' : '-code.h'
    glfw_protocols += custom_target(name + '-' + kind, input: 'src/external/glfw/deps/wayland/' + name + '.xml', output: name + '-client-protocol' + suffix, command: [scanner, kind, '@INPUT@', '@OUTPUT@'])
  endforeach
endforeach
raylib = static_library('raylib',
  ['src/rcore.c', 'src/rshapes.c', 'src/rtextures.c', 'src/rtext.c', 'src/utils.c', 'src/rglfw.c', 'src/rmodels.c', 'src/raudio.c'],
  glfw_protocols, layer_protocol,
  include_directories: [include_directories('.', 'src', 'src/external/glfw/include'), protocol_inc],
  dependencies: wayland_deps,
  c_args: ['-Wall', '-D_GNU_SOURCE', '-DPLATFORM_DESKTOP_GLFW', '-DGRAPHICS_API_OPENGL_33', '-D_GLFW_WAYLAND', '-Wno-missing-braces', '-Werror=pointer-arith', '-fno-strict-aliasing', '-Werror=implicit-function-declaration'],
  override_options: ['c_std=c99', 'optimization=2'],
  install: false,
)
