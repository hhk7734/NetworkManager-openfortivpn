project(
  'NetworkManager-openfortivpn', 'c',
  version: '0.1.0',
  license: 'GPL-2.0-or-later',
  meson_version: '>= 0.62.0',
  default_options: [
    'c_std=gnu11',
    'warning_level=2',
  ],
)

gnome = import('gnome')
i18n  = import('i18n')

cc = meson.get_compiler('c')

libnm_dep        = dependency('libnm',         version: '>= 1.40')
gio_dep          = dependency('gio-2.0',       version: '>= 2.66')
gtk4_dep         = dependency('gtk4',          version: '>= 4.6',  required: get_option('gtk4'))
libadwaita_dep   = dependency('libadwaita-1',  version: '>= 1.2',  required: get_option('gtk4'))
libsecret_dep    = dependency('libsecret-1',   version: '>= 0.18', required: get_option('gtk4'))

prefix     = get_option('prefix')
libdir     = get_option('libdir')
libexecdir = get_option('libexecdir')
datadir    = get_option('datadir')
sysconfdir = get_option('sysconfdir')

nm_vpn_service_dir = sysconfdir / 'NetworkManager' / 'VPN'
nm_plugin_dir      = libdir / 'NetworkManager'
dbus_system_dir    = datadir / 'dbus-1' / 'system.d'

openfortivpn_prog = find_program('openfortivpn', required: false)
if openfortivpn_prog.found()
  openfortivpn_path = openfortivpn_prog.full_path()
else
  openfortivpn_path = '/usr/sbin/openfortivpn'
endif

# .name template uses @VAR@ literal substitution — values must NOT be quoted.
name_conf = configuration_data()
name_conf.set('LIBEXECDIR', prefix / libexecdir)

configure_file(
  input:         'nm-openfortivpn-service.name.in',
  output:        'nm-openfortivpn-service.name',
  configuration: name_conf,
  install:       true,
  install_dir:   nm_vpn_service_dir,
  install_mode:  'rw-r--r--',
  install_tag:   'runtime',
)

# D-Bus policy: lets the service daemon (root) own the bus name family
# org.freedesktop.NetworkManager.openfortivpn[.Connection_N].
install_data(
  'nm-openfortivpn-service.conf',
  install_dir:  dbus_system_dir,
  install_mode: 'rw-r--r--',
)

# config.h generates `#define KEY value` lines, so string values need quoting.
cfg = configuration_data()
cfg.set_quoted('VERSION',                meson.project_version())
cfg.set_quoted('LIBEXECDIR',             prefix / libexecdir)
cfg.set_quoted('OPENFORTIVPN_PATH',      openfortivpn_path)
cfg.set_quoted('NM_OPENFORTIVPN_LOCALEDIR',  prefix / datadir / 'locale')
cfg.set_quoted('GETTEXT_PACKAGE',        'NetworkManager-openfortivpn')

configure_file(
  output:        'config.h',
  configuration: cfg,
)

inc = include_directories('.', 'shared')

subdir('src')
subdir('auth-dialog')
if gtk4_dep.found() and libadwaita_dep.found() and libsecret_dep.found()
  subdir('properties')
endif

summary({
  'prefix':       prefix,
  'libdir':       libdir,
  'libexecdir':   libexecdir,
  'GTK4 plugin':  gtk4_dep.found() and libadwaita_dep.found() and libsecret_dep.found(),
  'openfortivpn': openfortivpn_path,
}, section: 'Build configuration')
