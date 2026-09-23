/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * How the auth dialog's "Save password" choice is written to the connection.
 *
 * The GNOME secret agent saves a connection's secrets by deleting every
 * keyring item for it and writing back what NetworkManager hands it. An update
 * that turns saving on must therefore carry the password, or the saved
 * password is deleted instead. */

#include "config.h"

#include "nm-openfortivpn-service-defines.h"
#include "password-choice.h"

/* Record @save for @password in @s_vpn. Returns TRUE when the connection must
 * be updated with @s_vpn for the choice to take effect. */
gboolean
nm_openfortivpn_apply_password_choice(NMSettingVpn *s_vpn,
                                      gboolean      save,
                                      const char   *password)
{
    NMSettingSecretFlags flags = NM_SETTING_SECRET_FLAG_NONE;
    nm_setting_get_secret_flags(NM_SETTING(s_vpn), NM_OPENFORTIVPN_KEY_PASSWORD, &flags, NULL);

    if (save) {
        if (!password || !*password)
            return FALSE;
        nm_setting_set_secret_flags(NM_SETTING(s_vpn), NM_OPENFORTIVPN_KEY_PASSWORD,
                                    NM_SETTING_SECRET_FLAG_AGENT_OWNED, NULL);
        nm_setting_vpn_add_secret(s_vpn, NM_OPENFORTIVPN_KEY_PASSWORD, password);
        return TRUE;
    }

    if (flags & NM_SETTING_SECRET_FLAG_NOT_SAVED)
        return FALSE;

    /* Without the password, the agent's save deletes the stored one. */
    nm_setting_set_secret_flags(NM_SETTING(s_vpn), NM_OPENFORTIVPN_KEY_PASSWORD,
                                NM_SETTING_SECRET_FLAG_NOT_SAVED, NULL);
    nm_setting_vpn_remove_secret(s_vpn, NM_OPENFORTIVPN_KEY_PASSWORD);
    return TRUE;
}
