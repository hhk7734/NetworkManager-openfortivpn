if not (gtk4_dep.found() and libadwaita_dep.found())
  subdir_done()
endif

properties_resources = gnome.compile_resources(
  'nm-openfortivpn-resources',
  'nm-openfortivpn.gresource.xml',
  c_name:      'nm_openfortivpn',
  source_dir:  '.',
)

shared_module(
  'nm-vpn-plugin-openfortivpn',          # final filename: libnm-vpn-plugin-openfortivpn.so
  ['nm-openfortivpn-editor-plugin.c',
   'nm-openfortivpn-editor.c',
   properties_resources],
  include_directories: inc,
  dependencies:        [libnm_dep, gio_dep, gtk4_dep, libadwaita_dep],
  c_args:              ['-DG_LOG_DOMAIN="nm-openfortivpn-properties"'],
  install:             true,
  install_dir:         nm_plugin_dir,
)

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
