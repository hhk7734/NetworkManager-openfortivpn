if not (gtk4_dep.found() and libadwaita_dep.found())
  subdir_done()
endif

properties_resources = gnome.compile_resources(
  'nm-openfortivpn-resources',
  'nm-openfortivpn.gresource.xml',
  c_name:      'nm_openfortivpn',
  source_dir:  '.',
)

libnm_plugin = shared_module(
  'nm-vpn-plugin-openfortivpn',          # final filename: libnm-vpn-plugin-openfortivpn.so
  ['nm-openfortivpn-editor-plugin.c'],
  include_directories: inc,
  dependencies:        [libnm_dep],
  c_args:              ['-DG_LOG_DOMAIN="nm-openfortivpn-properties"'],
  install:             true,
  install_dir:         nm_plugin_dir,
)

properties_plugin = shared_module(
  'nm-openfortivpn-properties',          # final filename: libnm-openfortivpn-properties.so
  ['nm-openfortivpn-properties-plugin.c',
   'nm-openfortivpn-editor.c',
   properties_resources],
  include_directories: inc,
  dependencies:        [libnm_dep, gio_dep, gtk4_dep, libadwaita_dep],
  c_args:              ['-DG_LOG_DOMAIN="nm-openfortivpn-properties"'],
  install:             true,
  install_dir:         nm_plugin_dir,
)

readelf_prog = find_program('readelf', required: false)
if readelf_prog.found()
  test('libnm-plugin-deps',
       find_program('test-libnm-plugin-deps.sh'),
       args: [readelf_prog.full_path(), libnm_plugin.full_path()])
endif

test_editor = executable(
  'test-editor',
  ['test-editor.c',
   'nm-openfortivpn-editor.c',
   properties_resources],
  include_directories: inc,
  dependencies:        [libnm_dep, gio_dep, gtk4_dep],
  c_args:              ['-DG_LOG_DOMAIN="nm-openfortivpn-properties-test"'],
)

test('properties-editor', test_editor)

test_plugin_loader = executable(
  'test-plugin-loader',
  ['test-plugin-loader.c'],
  include_directories: inc,
  dependencies:        [libnm_dep, gtk4_dep],
  c_args:              ['-DG_LOG_DOMAIN="nm-openfortivpn-properties-test"'],
)

test('plugin-loader',
     test_plugin_loader,
     args: [libnm_plugin.full_path(), properties_plugin.full_path()])
