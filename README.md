# NetworkManager-openfortivpn

NetworkManager VPN plugin that drives [openfortivpn](https://github.com/adrienverge/openfortivpn)
to connect to Fortinet SSL VPN gateways.

Status: **early skeleton.** v1 scope is username + password auth, GTK 4 +
libadwaita UI, and IPv4 only.

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
sudo apt install \
  meson ninja-build pkg-config gcc \
  libnm-dev libglib2.0-dev libgtk-4-dev libadwaita-1-dev \
  openfortivpn
```

Fedora-equivalent: `meson ninja-build NetworkManager-libnm-devel glib2-devel
gtk4-devel libadwaita-devel openfortivpn`.

## Build and install

```sh
meson setup builddir --prefix=/usr --sysconfdir=/etc --libexecdir=/usr/libexec
meson compile -C builddir
sudo meson install -C builddir
```

After install, restart NetworkManager so it picks up the new VPN type:

```sh
sudo systemctl restart NetworkManager
```

The install places the following files on disk:

```
/etc/NetworkManager/VPN/nm-openfortivpn-service.name        NM service descriptor
/usr/share/dbus-1/system.d/nm-openfortivpn-service.conf     D-Bus policy (own bus name)
/usr/libexec/nm-openfortivpn-service                        VPN service daemon
/usr/libexec/nm-openfortivpn-auth-dialog                    password prompt
/usr/lib/<triplet>/NetworkManager/libnm-vpn-plugin-openfortivpn.so   GTK4 properties editor
```

## Uninstall

`ninja uninstall` reverses everything `meson install` placed on disk — including
the D-Bus policy at `/usr/share/dbus-1/system.d/nm-openfortivpn-service.conf`:

```sh
sudo ninja -C builddir uninstall
sudo systemctl restart NetworkManager
```

## Try it

```sh
nmcli connection add type vpn ifname '*' \
  con-name 'corp-openfortivpn' \
  vpn-type openfortivpn \
  vpn.data 'gateway=vpn.example.com,port=443,user=alice'
nmcli connection up corp-openfortivpn --ask
```

## Roadmap

- v1: password auth, IPv4, openfortivpn child process, parse stdout for IP
- v2: pppd plugin for reliable IP4Config push, IPv6, DNS, route table sync
- v3: client certificate, TOTP/2FA, SAML SSO
- v4: KDE plasma-nm KCM module

See `TODO.md` for the running list (not yet created).

## License

GPL-2.0-or-later. See `COPYING`.
