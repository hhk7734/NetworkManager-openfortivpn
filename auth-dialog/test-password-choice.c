/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "config.h"

#include <NetworkManager.h>

#include "nm-openfortivpn-service-defines.h"
#include "password-choice.h"

static NMSettingVpn *
new_vpn_setting(NMSettingSecretFlags flags)
{
    NMSettingVpn *s_vpn = NM_SETTING_VPN(nm_setting_vpn_new());
    nm_setting_set_secret_flags(NM_SETTING(s_vpn), NM_OPENFORTIVPN_KEY_PASSWORD, flags, NULL);
    return s_vpn;
}

static NMSettingSecretFlags
password_flags(NMSettingVpn *s_vpn)
{
    NMSettingSecretFlags flags = NM_SETTING_SECRET_FLAG_NONE;
    g_assert_true(nm_setting_get_secret_flags(NM_SETTING(s_vpn),
                                              NM_OPENFORTIVPN_KEY_PASSWORD,
                                              &flags, NULL));
    return flags;
}

/* A secret-less update makes the GNOME agent delete the saved password, so
 * choosing to save must always hand the password to NetworkManager. */
static void
test_save_from_ask_every_time(void)
{
    g_autoptr(NMSettingVpn) s_vpn = new_vpn_setting(NM_SETTING_SECRET_FLAG_NOT_SAVED);

    g_assert_true(nm_openfortivpn_apply_password_choice(s_vpn, TRUE, "typed"));
    g_assert_cmpint(password_flags(s_vpn), ==, NM_SETTING_SECRET_FLAG_AGENT_OWNED);
    g_assert_cmpstr(nm_setting_vpn_get_secret(s_vpn, NM_OPENFORTIVPN_KEY_PASSWORD), ==, "typed");
}

/* Also update when already saved: nmcli's agent doesn't store the password,
 * so this is what saves it when connecting with `nmcli --ask`. */
static void
test_save_when_already_saved(void)
{
    g_autoptr(NMSettingVpn) s_vpn = new_vpn_setting(NM_SETTING_SECRET_FLAG_AGENT_OWNED);

    g_assert_true(nm_openfortivpn_apply_password_choice(s_vpn, TRUE, "typed"));
    g_assert_cmpint(password_flags(s_vpn), ==, NM_SETTING_SECRET_FLAG_AGENT_OWNED);
    g_assert_cmpstr(nm_setting_vpn_get_secret(s_vpn, NM_OPENFORTIVPN_KEY_PASSWORD), ==, "typed");
}

static void
test_save_without_password_changes_nothing(void)
{
    g_autoptr(NMSettingVpn) s_vpn = new_vpn_setting(NM_SETTING_SECRET_FLAG_NOT_SAVED);

    g_assert_false(nm_openfortivpn_apply_password_choice(s_vpn, TRUE, ""));
    g_assert_cmpint(password_flags(s_vpn), ==, NM_SETTING_SECRET_FLAG_NOT_SAVED);
    g_assert_null(nm_setting_vpn_get_secret(s_vpn, NM_OPENFORTIVPN_KEY_PASSWORD));
}

static void
test_stop_saving(void)
{
    g_autoptr(NMSettingVpn) s_vpn = new_vpn_setting(NM_SETTING_SECRET_FLAG_AGENT_OWNED);
    nm_setting_vpn_add_secret(s_vpn, NM_OPENFORTIVPN_KEY_PASSWORD, "old");

    g_assert_true(nm_openfortivpn_apply_password_choice(s_vpn, FALSE, "typed"));
    g_assert_cmpint(password_flags(s_vpn), ==, NM_SETTING_SECRET_FLAG_NOT_SAVED);
    g_assert_null(nm_setting_vpn_get_secret(s_vpn, NM_OPENFORTIVPN_KEY_PASSWORD));
}

static void
test_not_saving_when_already_not_saved(void)
{
    g_autoptr(NMSettingVpn) s_vpn = new_vpn_setting(NM_SETTING_SECRET_FLAG_NOT_SAVED);

    g_assert_false(nm_openfortivpn_apply_password_choice(s_vpn, FALSE, "typed"));
    g_assert_cmpint(password_flags(s_vpn), ==, NM_SETTING_SECRET_FLAG_NOT_SAVED);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/openfortivpn/password-choice/save-from-ask-every-time",
                    test_save_from_ask_every_time);
    g_test_add_func("/openfortivpn/password-choice/save-when-already-saved",
                    test_save_when_already_saved);
    g_test_add_func("/openfortivpn/password-choice/save-without-password-changes-nothing",
                    test_save_without_password_changes_nothing);
    g_test_add_func("/openfortivpn/password-choice/stop-saving",
                    test_stop_saving);
    g_test_add_func("/openfortivpn/password-choice/not-saving-when-already-not-saved",
                    test_not_saving_when_already_not_saved);

    return g_test_run();
}
