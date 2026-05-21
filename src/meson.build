executable(
  'nm-openfortivpn-service',
  ['nm-openfortivpn-service.c'],
  include_directories: inc,
  dependencies:        [libnm_dep, gio_dep],
  c_args:              ['-DG_LOG_DOMAIN="nm-openfortivpn"'],
  install:             true,
  install_dir:         libexecdir,
)
