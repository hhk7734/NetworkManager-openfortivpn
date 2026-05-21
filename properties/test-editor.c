/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "config.h"

#include <gtk/gtk.h>
#include <NetworkManager.h>

#include "nm-openfortivpn-editor.h"
#include "nm-openfortivpn-service-defines.h"

static gboolean
ensure_gtk(void)
{
    if (!gtk_init_check()) {
        g_test_skip("GTK display is unavailable");
        return FALSE;
    }

    return TRUE;
}

static void
test_editor_constructs_without_critical_warnings(void)
{
    if (!ensure_gtk())
        return;

    GLogLevelFlags old_fatal = g_log_set_always_fatal(G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);

    g_autoptr(GError) error = NULL;
    g_autoptr(NMVpnEditor) editor = nm_openfortivpn_editor_new(NULL, &error);

    g_assert_no_error(error);
    g_assert_nonnull(editor);
    g_assert_nonnull(nm_vpn_editor_get_widget(editor));

    g_clear_object(&editor);
    g_log_set_always_fatal(old_fatal);
}

static void
test_saved_password_uses_system_secret_storage(void)
{
    if (!ensure_gtk())
        return;

    g_autoptr(GError) error = NULL;
    g_autoptr(NMVpnEditor) editor = nm_openfortivpn_editor_new(NULL, &error);
    g_assert_no_error(error);
    g_assert_nonnull(editor);

    g_autoptr(NMConnection) connection = nm_simple_connection_new();
    g_assert_true(nm_vpn_editor_update_connection(editor, connection, &error));
    g_assert_no_error(error);

    NMSettingVpn *s_vpn = nm_connection_get_setting_vpn(connection);
    g_assert_nonnull(s_vpn);

    NMSettingSecretFlags flags = NM_SETTING_SECRET_FLAG_AGENT_OWNED;
    g_assert_true(nm_setting_get_secret_flags(NM_SETTING(s_vpn),
                                              NM_OPENFORTIVPN_KEY_PASSWORD,
                                              &flags,
                                              NULL));
    g_assert_cmpint(flags, ==, NM_SETTING_SECRET_FLAG_NONE);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/openfortivpn/editor/constructs-without-critical-warnings",
                    test_editor_constructs_without_critical_warnings);
    g_test_add_func("/openfortivpn/editor/saved-password-uses-system-secret-storage",
                    test_saved_password_uses_system_secret_storage);

    return g_test_run();
}
