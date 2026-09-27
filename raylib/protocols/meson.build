protocol_inc = include_directories('.', is_system: true)
native_protocols = []
layer_protocol = []
foreach name : ['wlr-layer-shell-unstable-v1', 'wlr-foreign-toplevel-management-unstable-v1', 'ext-workspace-v1']
  code = custom_target(name + '-code', input: name + '.xml', output: name + '-client-protocol.c', command: [scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
  header = custom_target(name + '-header', input: name + '.xml', output: name + '-client-protocol.h', command: [scanner, 'client-header', '@INPUT@', '@OUTPUT@'], depends: code)
  if name == 'wlr-layer-shell-unstable-v1'
    layer_protocol = [header]
  else
    native_protocols += [header, code]
  endif
endforeach
