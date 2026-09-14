core_sources = files(
  'atm-theme.c',
  'atm-store.c',
  'atm-applier.c',
  'atm-components.c',
  'atm-discovery.c',
)

core_lib = static_library(
  'atm-core',
  core_sources,
  include_directories: include_directories('..'),
  dependencies: [glib_dep, gio_dep],
)

core_dep = declare_dependency(
  link_with: core_lib,
  include_directories: include_directories('.'),
  dependencies: [glib_dep, gio_dep],
)

executable(
  'axionis-theme-manager',
  ['main.c', 'atm-window.c'],
  include_directories: include_directories('..'),
  dependencies: [core_dep, gtk_dep, xapp_dep],
  install: true,
)

executable(
  'axionis-theme',
  'atm-cli.c',
  include_directories: include_directories('..'),
  dependencies: core_dep,
  install: true,
)
