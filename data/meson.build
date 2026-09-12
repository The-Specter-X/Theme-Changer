desktop_file = configure_file(
  input: 'io.axionis.ThemeManager.desktop.in',
  output: 'io.axionis.ThemeManager.desktop',
  copy: true,
  install: true,
  install_dir: datadir / 'applications',
)

install_data(
  'io.axionis.ThemeManager.metainfo.xml',
  install_dir: datadir / 'metainfo',
)

install_data(
  'icons/io.axionis.ThemeManager.svg',
  install_dir: datadir / 'icons/hicolor/scalable/apps',
)
