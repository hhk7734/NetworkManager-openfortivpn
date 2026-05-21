if not (gtk4_dep.found() and libadwaita_dep.found())
  subdir_done()
endif

executable(
  'nm-openfortivpn-auth-dialog',
  ['main.c'],
  include_directories: inc,
  dependencies:        [libnm_dep, gio_dep, gtk4_dep, libadwaita_dep],
  c_args:              ['-DG_LOG_DOMAIN="nm-openfortivpn-auth"'],
  install:             true,
  install_dir:         libexecdir,
)
