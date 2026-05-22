if not (gtk4_dep.found() and libadwaita_dep.found() and libsecret_dep.found())
  subdir_done()
endif

auth_dialog = executable(
  'nm-openfortivpn-auth-dialog',
  ['main.c'],
  include_directories: inc,
  dependencies:        [libnm_dep, gio_dep, gtk4_dep, libadwaita_dep, libsecret_dep],
  c_args:              ['-DG_LOG_DOMAIN="nm-openfortivpn-auth"'],
  install:             true,
  install_dir:         libexecdir,
)

test(
  'auth-dialog-old-style-output',
  find_program('test-old-style-output.sh'),
  args: [auth_dialog],
)
