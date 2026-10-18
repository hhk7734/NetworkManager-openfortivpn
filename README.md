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

## Reconnecting after the gateway drops the tunnel

If the tunnel goes down on its own once it has been up, for example when the
gateway ends the session at its time limit, the VPN service restarts
openfortivpn with the password it was given at connect. It waits 5 s before
the first attempt and doubles the wait after each one, up to 60 s, until the
tunnel is back. The wait starts over at 5 s once a tunnel has stayed up for a
minute.

If the gateway's name doesn't resolve during an attempt, the service connects
to the address it last resolved to and still sends the name as the TLS server
name (SNI). Expect this whenever the VPN sets DNS servers: they replace the
other network's, and NetworkManager doesn't switch those back on while the
VPN is reconnecting. openfortivpn checks the
gateway's certificate against that address, so this works when `trusted-cert`
is set (or the certificate lists the address).

While it reconnects, NetworkManager shows the VPN as activating
(`nmcli connection show --active`), and the old interface's address, routes
and DNS servers go away with it. Once the tunnel is back, NetworkManager
configures the new interface and shows the VPN as activated again.

This only works while that VPN session is still running:

- `nmcli connection down`, or turning the VPN off in the UI, ends the session
  and stops any further attempts.
- If the gateway rejects the login on any attempt, the service stops trying
  and the VPN fails with "Invalid secrets" (`login-failed` in the
  NetworkManager log).
- After 10 attempts in a row that don't bring the tunnel back, about 7
  minutes, the service gives up and the VPN fails (`connect-failed` in the
  NetworkManager log). NetworkManager then takes the VPN down and switches the
  other network's DNS servers back on.
- Once the session has ended, nothing brings it back. That includes the VPN
  service or NetworkManager exiting or restarting, or the network the VPN runs
  over going down. Connect again, as you would the first time.

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
