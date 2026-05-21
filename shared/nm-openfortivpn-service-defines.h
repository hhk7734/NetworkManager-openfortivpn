/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NM_OPENFORTIVPN_SERVICE_DEFINES_H
#define NM_OPENFORTIVPN_SERVICE_DEFINES_H

/* D-Bus identity of the VPN service. Must match the [VPN Connection] service
 * field in nm-openfortivpn-service.name and the address claimed by the daemon. */
#define NM_DBUS_SERVICE_OPENFORTIVPN  "org.freedesktop.NetworkManager.openfortivpn"
#define NM_DBUS_INTERFACE_OPENFORTIVPN "org.freedesktop.NetworkManager.openfortivpn"
#define NM_DBUS_PATH_OPENFORTIVPN     "/org/freedesktop/NetworkManager/openfortivpn"

/* Keys for the [vpn] data dict in the NMSettingVpn. Stay stable across
 * releases — they are persisted in connection profiles. */
#define NM_OPENFORTIVPN_KEY_GATEWAY        "gateway"
#define NM_OPENFORTIVPN_KEY_PORT           "port"
#define NM_OPENFORTIVPN_KEY_USER           "user"
#define NM_OPENFORTIVPN_KEY_TRUSTED_CERT   "trusted-cert"     /* SHA-256 pin */
#define NM_OPENFORTIVPN_KEY_NO_DTLS        "no-dtls"          /* "yes"/"no" */
#define NM_OPENFORTIVPN_KEY_INSECURE_SSL   "insecure-ssl"     /* "yes"/"no" */

/* Keys for the [vpn-secrets] dict. */
#define NM_OPENFORTIVPN_KEY_PASSWORD       "password"

#endif /* NM_OPENFORTIVPN_SERVICE_DEFINES_H */
