# NetworkManager-openfortivpn

NetworkManager VPN plugin that drives [openfortivpn](https://github.com/adrienverge/openfortivpn)
to connect to Fortinet SSL VPN gateways.

Status: **v0.3.0 works on Ubuntu 26.04.** Current scope is username +
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

## Using nmcli

Everything the connection editor does can also be done from a terminal.

Create a connection:

```sh
nmcli connection add type vpn ifname '*' \
  con-name <name> \
  vpn-type openfortivpn \
  vpn.data 'gateway=<gateway>,port=443,user=<username>'
```

Connect, typing the password in the terminal:

```sh
nmcli connection up <name> --ask
```

Disconnect, inspect, and delete:

```sh
nmcli connection down <name>
nmcli connection show <name> | grep '^vpn\.'
nmcli connection delete <name>
```

### Connection options

All options live in `vpn.data`:

| Key            | Required | Meaning                                                        |
| -------------- | -------- | -------------------------------------------------------------- |
| `gateway`      | yes      | Gateway host name or IP address                                |
| `user`         | yes      | Username                                                       |
| `port`         | no       | Gateway port; openfortivpn's default (443) when unset          |
| `trusted-cert` | no       | SHA-256 digest of the gateway certificate to trust             |
| `no-dtls`      | no       | `yes` to pass `--no-dtls`                                      |
| `insecure-ssl` | no       | `yes` to pass `--insecure-ssl`                                 |

Add or change one option without retyping the rest with `+vpn.data`, and
remove one with `-vpn.data`:

```sh
nmcli connection modify <name> +vpn.data 'trusted-cert=<sha256-digest>'
nmcli connection modify <name> -vpn.data trusted-cert
```

If the gateway certificate is self-signed or not in the system store,
openfortivpn refuses it and logs the digest to pin; find it with
`journalctl -u NetworkManager | grep -i trusted-cert`.

### Password storage

`password-flags` in `vpn.data` picks where the password is kept. **Save
password** in the password prompt or the connection editor switches between
the first two:

```sh
# Saved in your login keyring by your secret agent (GNOME Shell or nm-applet)
nmcli connection modify <name> +vpn.data 'password-flags=1'

# Asked on every connect; a previously saved password is removed
nmcli connection modify <name> +vpn.data 'password-flags=2'
```

With `password-flags=1`, any connection update that carries no password makes
the GNOME agent delete the saved one. That includes a plain `nmcli connection
modify` while disconnected, so enter the password again on the next connect.
The prompt itself never writes the keyring; it saves your choice by updating
the connection, with the password, a couple of seconds after it closes.

For a headless machine with no secret agent, `password-flags=0` lets
NetworkManager store the password itself, in plain text in the root-only file
under `/etc/NetworkManager/system-connections/` (the command below also leaves
it in your shell history):

```sh
nmcli connection modify <name> \
  +vpn.data 'password-flags=0' \
  vpn.secrets 'password=<password>'
nmcli connection up <name>
```

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
