if get_option('tests')
  glib_compile_schemas = find_program('glib-compile-schemas')
  test_schemas = custom_target(
    'test-gsettings-schemas',
    input: 'org.axionis.test.gschema.xml',
    output: 'gschemas.compiled',
    command: [
      glib_compile_schemas,
      '--strict',
      '--targetdir', meson.current_build_dir(),
      meson.current_source_dir(),
    ],
    build_by_default: true,
  )

  test_core = executable(
    'test-core',
    'test-core.c',
    include_directories: include_directories('..'),
    dependencies: core_dep,
    c_args: '-DTEST_SOURCE_DIR="' + meson.current_source_dir() + '"',
  )

  test(
    'core',
    test_core,
    depends: test_schemas,
    env: [
      'GSETTINGS_SCHEMA_DIR=' + meson.current_build_dir(),
      'GSETTINGS_BACKEND=memory',
    ],
  )
endif
