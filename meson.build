project(
  'axionis-theme-manager',
  'c',
  version: '0.3.1',
  license: 'GPL-3.0-or-later',
  meson_version: '>= 0.61.0',
  default_options: [
    'c_std=c11',
    'warning_level=3',
    'werror=true',
  ],
)

gnome = import('gnome')

prefix = get_option('prefix')
datadir = prefix / get_option('datadir')
localedir = prefix / get_option('localedir')

conf = configuration_data()
conf.set_quoted('ATM_VERSION', meson.project_version())
conf.set_quoted('ATM_DATADIR', datadir)
conf.set_quoted('ATM_LOCALEDIR', localedir)
configure_file(output: 'config.h', configuration: conf)

glib_dep = dependency('glib-2.0', version: '>= 2.74')
gio_dep = dependency('gio-2.0', version: '>= 2.74')
gtk_dep = dependency('gtk+-3.0', version: '>= 3.24')
xapp_dep = dependency('xapp', version: '>= 2.2')

subdir('src')
subdir('data')
subdir('tests')

install_subdir(
  'themes',
  install_dir: datadir / 'axionis-theme-manager',
)

summary({
  'Version': meson.project_version(),
  'Prefix': prefix,
  'Tests': get_option('tests'),
}, section: 'Axionis Theme Manager')
