executable(
  'nm-openfortivpn-service',
  ['nm-openfortivpn-service.c'],
  include_directories: inc,
  dependencies:        [libnm_dep, gio_dep],
  c_args:              ['-DG_LOG_DOMAIN="nm-openfortivpn"'],
  install:             true,
  install_dir:         libexecdir,
)

# Same service, spawning a fake openfortivpn, for the lifecycle test.
test_service = executable(
  'nm-openfortivpn-service-test',
  ['nm-openfortivpn-service.c'],
  include_directories: inc,
  dependencies:        [libnm_dep, gio_dep],
  c_args:              ['-DG_LOG_DOMAIN="nm-openfortivpn"',
                        '-DOPENFORTIVPN_TEST_PATH="@0@"'.format(
                          meson.current_source_dir() / 'test-fake-openfortivpn')],
)

test('service-lifecycle',
     find_program('test-service-lifecycle.sh'),
     args:       [test_service.full_path()],
     depends:    [test_service],
     is_parallel: false,
     timeout:    120)
