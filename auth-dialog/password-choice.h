/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NM_OPENFORTIVPN_PASSWORD_CHOICE_H
#define NM_OPENFORTIVPN_PASSWORD_CHOICE_H

#include <NetworkManager.h>

G_BEGIN_DECLS

gboolean nm_openfortivpn_apply_password_choice(NMSettingVpn *s_vpn,
                                               gboolean      save,
                                               const char   *password);

G_END_DECLS

#endif /* NM_OPENFORTIVPN_PASSWORD_CHOICE_H */
