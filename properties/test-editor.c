/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "config.h"

#include <gtk/gtk.h>
#include <NetworkManager.h>

#include "nm-openfortivpn-editor.h"

static void
test_editor_constructs_without_critical_warnings(void)
{
    if (!gtk_init_check()) {
        g_test_skip("GTK display is unavailable");
        return;
    }

    GLogLevelFlags old_fatal = g_log_set_always_fatal(G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);

    g_autoptr(GError) error = NULL;
    g_autoptr(NMVpnEditor) editor = nm_openfortivpn_editor_new(NULL, &error);

    g_assert_no_error(error);
    g_assert_nonnull(editor);
    g_assert_nonnull(nm_vpn_editor_get_widget(editor));

    g_clear_object(&editor);
    g_log_set_always_fatal(old_fatal);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/openfortivpn/editor/constructs-without-critical-warnings",
                    test_editor_constructs_without_critical_warnings);

    return g_test_run();
}
