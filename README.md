# NetworkManager-openfortivpn

NetworkManager VPN plugin that drives [openfortivpn](https://github.com/adrienverge/openfortivpn)
to connect to Fortinet SSL VPN gateways.

Status: **v0.4.0 works on Ubuntu 26.04.** Current scope is username +
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
gateway ends the session at its time limit, the VPN fails (`connect-failed`
in the NetworkManager log). NetworkManager takes the VPN down, which brings
the other network's DNS servers back. To have NetworkManager connect again
right away, make the connection persistent:

```sh
nmcli connection modify <name> vpn.persistent yes
```

NetworkManager retries once, as a new activation. That needs the password
again: a password stored with `password-flags=0` works unattended, one kept
by a secret agent only while you're logged in.

Until a tunnel is up, the VPN service restarts openfortivpn after an attempt
that fails, so the retry survives a gateway that is out of reach for a
while. It waits 5 s before the first restart and doubles the wait after each
one, up to 60 s. Attempts stop when:

- `nmcli connection down`, or turning the VPN off in the UI, ends the
  activation.
- The gateway rejects the login. The VPN fails with "Invalid secrets"
  (`login-failed` in the NetworkManager log).
- NetworkManager's `vpn.timeout` runs out, 60 s by default, about 4 attempts.
  Raise it to keep trying through longer outages, for example 10 minutes:

  ```sh
  nmcli connection modify <name> vpn.timeout 600
  ```

- 10 restarts in a row have failed, about 7 minutes, for a `vpn.timeout`
  longer than that.

Once attempts have stopped, nothing brings the VPN back; connect again as you
would the first time. The same goes for a first connect: a mistyped or
unreachable gateway fails only once attempts stop, not at once.

The service doesn't restart a lost tunnel itself: openfortivpn would bring it
back on a new interface, and NetworkManager 1.54 keeps the VPN's DNS
settings under the old one. Once the VPN went down, the other network's DNS
stayed off until NetworkManager restarted.

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
