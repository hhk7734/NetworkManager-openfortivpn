# NetworkManager-openfortivpn

NetworkManager VPN plugin that drives [openfortivpn](https://github.com/adrienverge/openfortivpn)
to connect to Fortinet SSL VPN gateways.

Status: **v0.1.0 works on Ubuntu 26.04.** Current scope is username +
password auth, Secret Service password storage, GTK 4 + libadwaita UI, and
IPv4 only.

## Layout

```
src/                  D-Bus VPN service daemon (NMVpnServicePlugin subclass)
auth-dialog/          GTK4 password prompt invoked by nm-applet / GNOME
properties/           GTK4 connection-editor plugin (gateway, username, ...)
shared/               Constants and helpers used by all components
nm-openfortivpn-service.name.in   NM service descriptor template
```

## Build dependencies (Ubuntu/Debian names)

```sh
mise run deps:ubuntu
```

## Build and install

```sh
mise run install
```

The install task restarts NetworkManager so it picks up the new VPN type.

The install places the following files on disk:

```
/etc/NetworkManager/VPN/nm-openfortivpn-service.name        NM service descriptor
/usr/share/dbus-1/system.d/nm-openfortivpn-service.conf     D-Bus policy (own bus name)
/usr/libexec/nm-openfortivpn-service                        VPN service daemon
/usr/libexec/nm-openfortivpn-auth-dialog                    password prompt
/usr/lib/<triplet>/NetworkManager/libnm-vpn-plugin-openfortivpn.so   lightweight libnm plugin
/usr/lib/<triplet>/NetworkManager/libnm-openfortivpn-properties.so    GTK4 properties editor
```

## Passwords

Passwords are entered at connection time. Check **Save password** in the auth
dialog to store the password in the user's Secret Service keyring.

## Uninstall

`ninja uninstall` reverses everything `meson install` placed on disk — including
the D-Bus policy at `/usr/share/dbus-1/system.d/nm-openfortivpn-service.conf`:

```sh
mise run uninstall
```

## Try it

```sh
mise run try
```

## Roadmap

- v1: password auth, IPv4, openfortivpn child process, parse stdout for IP
- v2: pppd plugin for reliable IP4Config push, IPv6, DNS, route table sync
- v3: client certificate, TOTP/2FA, SAML SSO
- v4: KDE plasma-nm KCM module

See `TODO.md` for the running list (not yet created).

## License

GPL-2.0-or-later. See `COPYING`.
