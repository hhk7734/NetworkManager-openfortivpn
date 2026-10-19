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
assert_password_flags(NMSettingVpn *s_vpn, NMSettingSecretFlags expected)
{
    NMSettingSecretFlags flags = NM_SETTING_SECRET_FLAG_AGENT_OWNED;
    g_assert_true(nm_setting_get_secret_flags(NM_SETTING(s_vpn),
                                              NM_OPENFORTIVPN_KEY_PASSWORD,
                                              &flags,
                                              NULL));
    g_assert_cmpint(flags, ==, expected);
}

static NMConnection *
new_connection_with_password_flags(NMSettingSecretFlags flags, const char *password)
{
    NMConnection *connection = nm_simple_connection_new();
    NMSettingVpn *s_vpn = NM_SETTING_VPN(nm_setting_vpn_new());

    g_object_set(s_vpn, NM_SETTING_VPN_SERVICE_TYPE, NM_DBUS_SERVICE_OPENFORTIVPN, NULL);
    nm_setting_set_secret_flags(NM_SETTING(s_vpn),
                                NM_OPENFORTIVPN_KEY_PASSWORD,
                                flags,
                                NULL);
    if (password)
        nm_setting_vpn_add_secret(s_vpn, NM_OPENFORTIVPN_KEY_PASSWORD, password);

    nm_connection_add_setting(connection, NM_SETTING(s_vpn));
    return connection;
}

static GtkWidget *
find_active_switch(GtkWidget *widget)
{
    if (GTK_IS_SWITCH(widget) && gtk_switch_get_active(GTK_SWITCH(widget)))
        return widget;

    for (GtkWidget *child = gtk_widget_get_first_child(widget);
         child;
         child = gtk_widget_get_next_sibling(child)) {
        GtkWidget *found = find_active_switch(child);
        if (found)
            return found;
    }

    return NULL;
}

static GtkWidget *
find_widget_of_type(GtkWidget *widget, GType type)
{
    if (G_TYPE_CHECK_INSTANCE_TYPE(widget, type))
        return widget;

    for (GtkWidget *child = gtk_widget_get_first_child(widget);
         child;
         child = gtk_widget_get_next_sibling(child)) {
        GtkWidget *found = find_widget_of_type(child, type);
        if (found)
            return found;
    }

    return NULL;
}

static NMSettingVpn *
update_into_new_connection(NMVpnEditor *editor, NMConnection **out)
{
    g_autoptr(GError) error = NULL;

    *out = nm_simple_connection_new();
    g_assert_true(nm_vpn_editor_update_connection(editor, *out, &error));
    g_assert_no_error(error);

    NMSettingVpn *s_vpn = nm_connection_get_setting_vpn(*out);
    g_assert_nonnull(s_vpn);
    return s_vpn;
}

static void
on_editor_changed(G_GNUC_UNUSED NMVpnEditor *editor, gpointer user_data)
{
    guint *count = user_data;
    (*count)++;
}

static void
test_switch_change_emits_changed(void)
{
    if (!ensure_gtk())
        return;

    g_autoptr(GError) error = NULL;
    g_autoptr(NMVpnEditor) editor = nm_openfortivpn_editor_new(NULL, &error);
    g_assert_no_error(error);
    g_assert_nonnull(editor);

    guint changed_count = 0;
    g_signal_connect(editor, "changed", G_CALLBACK(on_editor_changed), &changed_count);

    GtkWidget *save_switch = find_active_switch(GTK_WIDGET(nm_vpn_editor_get_widget(editor)));
    g_assert_nonnull(save_switch);

    gtk_switch_set_active(GTK_SWITCH(save_switch), FALSE);

    while (g_main_context_iteration(NULL, FALSE)) { }

    g_assert_cmpuint(changed_count, >, 0);
}

static void
test_saved_password_becomes_agent_owned(void)
{
    if (!ensure_gtk())
        return;

    g_autoptr(GError) error = NULL;
    g_autoptr(NMConnection) input =
        new_connection_with_password_flags(NM_SETTING_SECRET_FLAG_NONE, "secret");
    g_autoptr(NMVpnEditor) editor = nm_openfortivpn_editor_new(input, &error);
    g_assert_no_error(error);
    g_assert_nonnull(editor);

    g_autoptr(NMConnection) output = nm_simple_connection_new();
    g_assert_true(nm_vpn_editor_update_connection(editor, output, &error));
    g_assert_no_error(error);

    NMSettingVpn *s_vpn = nm_connection_get_setting_vpn(output);
    g_assert_nonnull(s_vpn);
    assert_password_flags(s_vpn, NM_SETTING_SECRET_FLAG_AGENT_OWNED);
    g_assert_cmpstr(nm_setting_vpn_get_secret(s_vpn, NM_OPENFORTIVPN_KEY_PASSWORD), ==, "secret");
}

static void
test_agent_owned_password_stays_agent_owned(void)
{
    if (!ensure_gtk())
        return;

    g_autoptr(GError) error = NULL;
    g_autoptr(NMConnection) input =
        new_connection_with_password_flags(NM_SETTING_SECRET_FLAG_AGENT_OWNED, NULL);
    g_autoptr(NMVpnEditor) editor = nm_openfortivpn_editor_new(input, &error);
    g_assert_no_error(error);
    g_assert_nonnull(editor);

    g_autoptr(NMConnection) output = nm_simple_connection_new();
    g_assert_true(nm_vpn_editor_update_connection(editor, output, &error));
    g_assert_no_error(error);

    NMSettingVpn *s_vpn = nm_connection_get_setting_vpn(output);
    g_assert_nonnull(s_vpn);
    assert_password_flags(s_vpn, NM_SETTING_SECRET_FLAG_AGENT_OWNED);
    g_assert_null(nm_setting_vpn_get_secret(s_vpn, NM_OPENFORTIVPN_KEY_PASSWORD));
}

/* A secret-less update makes the GNOME agent delete the keyring item, so the
 * editor must hand back the password it was given. */
static void
test_agent_owned_password_is_carried_through(void)
{
    if (!ensure_gtk())
        return;

    g_autoptr(GError) error = NULL;
    g_autoptr(NMConnection) input =
        new_connection_with_password_flags(NM_SETTING_SECRET_FLAG_AGENT_OWNED, "secret");
    g_autoptr(NMVpnEditor) editor = nm_openfortivpn_editor_new(input, &error);
    g_assert_no_error(error);
    g_assert_nonnull(editor);

    g_autoptr(NMConnection) output = nm_simple_connection_new();
    g_assert_true(nm_vpn_editor_update_connection(editor, output, &error));
    g_assert_no_error(error);

    NMSettingVpn *s_vpn = nm_connection_get_setting_vpn(output);
    g_assert_nonnull(s_vpn);
    assert_password_flags(s_vpn, NM_SETTING_SECRET_FLAG_AGENT_OWNED);
    g_assert_cmpstr(nm_setting_vpn_get_secret(s_vpn, NM_OPENFORTIVPN_KEY_PASSWORD), ==, "secret");
}

static void
test_turning_off_save_drops_password(void)
{
    if (!ensure_gtk())
        return;

    g_autoptr(GError) error = NULL;
    g_autoptr(NMConnection) input =
        new_connection_with_password_flags(NM_SETTING_SECRET_FLAG_AGENT_OWNED, "secret");
    g_autoptr(NMVpnEditor) editor = nm_openfortivpn_editor_new(input, &error);
    g_assert_no_error(error);
    g_assert_nonnull(editor);

    GtkWidget *save_switch = find_active_switch(GTK_WIDGET(nm_vpn_editor_get_widget(editor)));
    g_assert_nonnull(save_switch);
    gtk_switch_set_active(GTK_SWITCH(save_switch), FALSE);

    g_autoptr(NMConnection) output = nm_simple_connection_new();
    g_assert_true(nm_vpn_editor_update_connection(editor, output, &error));
    g_assert_no_error(error);

    NMSettingVpn *s_vpn = nm_connection_get_setting_vpn(output);
    g_assert_nonnull(s_vpn);
    assert_password_flags(s_vpn, NM_SETTING_SECRET_FLAG_NOT_SAVED);
    g_assert_null(nm_setting_vpn_get_secret(s_vpn, NM_OPENFORTIVPN_KEY_PASSWORD));
}

static void
test_not_saved_password_stays_not_saved(void)
{
    if (!ensure_gtk())
        return;

    g_autoptr(GError) error = NULL;
    g_autoptr(NMConnection) input =
        new_connection_with_password_flags(NM_SETTING_SECRET_FLAG_NOT_SAVED, NULL);
    g_autoptr(NMVpnEditor) editor = nm_openfortivpn_editor_new(input, &error);
    g_assert_no_error(error);
    g_assert_nonnull(editor);

    g_autoptr(NMConnection) output = nm_simple_connection_new();
    g_assert_true(nm_vpn_editor_update_connection(editor, output, &error));
    g_assert_no_error(error);

    NMSettingVpn *s_vpn = nm_connection_get_setting_vpn(output);
    g_assert_nonnull(s_vpn);
    assert_password_flags(s_vpn, NM_SETTING_SECRET_FLAG_NOT_SAVED);
    g_assert_null(nm_setting_vpn_get_secret(s_vpn, NM_OPENFORTIVPN_KEY_PASSWORD));
}

/* The editor replaces the VPN setting whole, so a save must keep these. */
static void
test_reconnect_settings_are_kept(void)
{
    if (!ensure_gtk())
        return;

    g_autoptr(GError) error = NULL;
    g_autoptr(NMConnection) input =
        new_connection_with_password_flags(NM_SETTING_SECRET_FLAG_AGENT_OWNED, NULL);
    g_object_set(nm_connection_get_setting_vpn(input),
                 NM_SETTING_VPN_PERSISTENT, TRUE,
                 NM_SETTING_VPN_TIMEOUT,    600u,
                 NULL);
    g_autoptr(NMVpnEditor) editor = nm_openfortivpn_editor_new(input, &error);
    g_assert_no_error(error);
    g_assert_nonnull(editor);

    g_autoptr(NMConnection) output = NULL;
    NMSettingVpn *s_vpn = update_into_new_connection(editor, &output);
    g_assert_true(nm_setting_vpn_get_persistent(s_vpn));
    g_assert_cmpuint(nm_setting_vpn_get_timeout(s_vpn), ==, 600);
}

static void
test_reconnect_settings_default_off(void)
{
    if (!ensure_gtk())
        return;

    g_autoptr(GError) error = NULL;
    g_autoptr(NMVpnEditor) editor = nm_openfortivpn_editor_new(NULL, &error);
    g_assert_no_error(error);
    g_assert_nonnull(editor);

    g_autoptr(NMConnection) output = NULL;
    NMSettingVpn *s_vpn = update_into_new_connection(editor, &output);
    g_assert_false(nm_setting_vpn_get_persistent(s_vpn));
    g_assert_cmpuint(nm_setting_vpn_get_timeout(s_vpn), ==, 0);
}

static void
test_timeout_change_is_saved(void)
{
    if (!ensure_gtk())
        return;

    g_autoptr(GError) error = NULL;
    g_autoptr(NMVpnEditor) editor = nm_openfortivpn_editor_new(NULL, &error);
    g_assert_no_error(error);
    g_assert_nonnull(editor);

    guint changed_count = 0;
    g_signal_connect(editor, "changed", G_CALLBACK(on_editor_changed), &changed_count);

    GtkWidget *spin = find_widget_of_type(GTK_WIDGET(nm_vpn_editor_get_widget(editor)),
                                          GTK_TYPE_SPIN_BUTTON);
    g_assert_nonnull(spin);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), 300);

    while (g_main_context_iteration(NULL, FALSE)) { }
    g_assert_cmpuint(changed_count, >, 0);

    g_autoptr(NMConnection) output = NULL;
    NMSettingVpn *s_vpn = update_into_new_connection(editor, &output);
    g_assert_cmpuint(nm_setting_vpn_get_timeout(s_vpn), ==, 300);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/openfortivpn/editor/constructs-without-critical-warnings",
                    test_editor_constructs_without_critical_warnings);
    g_test_add_func("/openfortivpn/editor/switch-change-emits-changed",
                    test_switch_change_emits_changed);
    g_test_add_func("/openfortivpn/editor/saved-password-becomes-agent-owned",
                    test_saved_password_becomes_agent_owned);
    g_test_add_func("/openfortivpn/editor/agent-owned-password-stays-agent-owned",
                    test_agent_owned_password_stays_agent_owned);
    g_test_add_func("/openfortivpn/editor/agent-owned-password-is-carried-through",
                    test_agent_owned_password_is_carried_through);
    g_test_add_func("/openfortivpn/editor/turning-off-save-drops-password",
                    test_turning_off_save_drops_password);
    g_test_add_func("/openfortivpn/editor/not-saved-password-stays-not-saved",
                    test_not_saved_password_stays_not_saved);
    g_test_add_func("/openfortivpn/editor/reconnect-settings-are-kept",
                    test_reconnect_settings_are_kept);
    g_test_add_func("/openfortivpn/editor/reconnect-settings-default-off",
                    test_reconnect_settings_default_off);
    g_test_add_func("/openfortivpn/editor/timeout-change-is-saved",
                    test_timeout_change_is_saved);

    return g_test_run();
}
