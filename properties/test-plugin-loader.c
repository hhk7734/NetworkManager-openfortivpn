/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "config.h"

#include <gtk/gtk.h>
#include <NetworkManager.h>

#include "nm-openfortivpn-service-defines.h"

static NMVpnPluginInfo *
new_test_plugin_info(const char *properties_plugin, GError **error)
{
    g_autoptr(GKeyFile) keyfile = g_key_file_new();

    g_key_file_set_string(keyfile,
                          NM_VPN_PLUGIN_INFO_KF_GROUP_CONNECTION,
                          "name",
                          "openfortivpn");
    g_key_file_set_string(keyfile,
                          NM_VPN_PLUGIN_INFO_KF_GROUP_CONNECTION,
                          "service",
                          NM_DBUS_SERVICE_OPENFORTIVPN);
    g_key_file_set_string(keyfile,
                          NM_VPN_PLUGIN_INFO_KF_GROUP_CONNECTION,
                          "program",
                          "nm-openfortivpn-service");
    g_key_file_set_string(keyfile,
                          NM_VPN_PLUGIN_INFO_KF_GROUP_LIBNM,
                          "plugin",
                          "libnm-vpn-plugin-openfortivpn.so");
    g_key_file_set_string(keyfile,
                          NM_VPN_PLUGIN_INFO_KF_GROUP_GNOME,
                          "properties",
                          properties_plugin);

    return nm_vpn_plugin_info_new_with_data("nm-openfortivpn-service.name", keyfile, error);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    if (argc != 3) {
        g_printerr("usage: %s LIBNM_PLUGIN PROPERTIES_PLUGIN\n", argv[0]);
        return 2;
    }

    if (!gtk_init_check()) {
        g_test_message("GTK display is unavailable");
        return 77;
    }

    GLogLevelFlags old_fatal = g_log_set_always_fatal(G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);

    g_autoptr(GError) error = NULL;
    g_autoptr(NMVpnPluginInfo) plugin_info = new_test_plugin_info(argv[2], &error);
    g_assert_no_error(error);
    g_assert_nonnull(plugin_info);

    g_autoptr(NMVpnEditorPlugin) plugin =
        nm_vpn_editor_plugin_load_from_file(argv[1],
                                            NM_DBUS_SERVICE_OPENFORTIVPN,
                                            -1,
                                            NULL,
                                            NULL,
                                            &error);
    g_assert_no_error(error);
    g_assert_nonnull(plugin);

    nm_vpn_editor_plugin_set_plugin_info(plugin, plugin_info);

    g_autoptr(NMVpnEditor) editor = nm_vpn_editor_plugin_get_editor(plugin, NULL, &error);
    g_assert_no_error(error);
    g_assert_nonnull(editor);
    g_assert_nonnull(nm_vpn_editor_get_widget(editor));

    g_clear_object(&editor);
    g_log_set_always_fatal(old_fatal);

    return 0;
}
