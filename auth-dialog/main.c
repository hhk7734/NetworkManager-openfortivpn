/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * nm-openfortivpn-auth-dialog -- prompts for the FortiVPN password when NM
 * (via nm-applet, GNOME's Network panel, or `nmcli --ask`) needs secrets.
 *
 * Protocol (matches nm-openvpn / nm-openconnect):
 *   argv:    -u UUID -n NAME -s SERVICE [-r reprompt] [-i allow_interaction]
 *   stdin:   keyfile-formatted VPN data + existing secrets, terminated by EOF
 *   stdout:  KEY\nVALUE\n for each secret to return, then a blank line
 *   stdin:   then "QUIT\n" from NM signals the dialog can exit
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <gtk/gtk.h>
#include <adwaita.h>
#include <NetworkManager.h>
#include <nm-vpn-service-plugin.h>

#include "nm-openfortivpn-service-defines.h"

/* --- CLI options --------------------------------------------------------- */

static char    *opt_uuid       = NULL;
static char    *opt_name       = NULL;
static char    *opt_service    = NULL;
static gboolean opt_reprompt   = FALSE;
static gboolean opt_allow_interaction = FALSE;
static gboolean opt_external_ui = FALSE;

static GOptionEntry option_entries[] = {
    { "uuid",    'u', 0, G_OPTION_ARG_STRING, &opt_uuid,    "Connection UUID",    "UUID" },
    { "name",    'n', 0, G_OPTION_ARG_STRING, &opt_name,    "Connection name",    "NAME" },
    { "service", 's', 0, G_OPTION_ARG_STRING, &opt_service, "VPN service type",   "SERVICE" },
    { "reprompt", 'r', 0, G_OPTION_ARG_NONE,  &opt_reprompt, "Force re-prompt",   NULL },
    { "allow-interaction", 'i', 0, G_OPTION_ARG_NONE, &opt_allow_interaction,
      "GUI interaction is permitted", NULL },
    { "external-ui-mode", 0, 0, G_OPTION_ARG_NONE, &opt_external_ui,
      "External UI mode (unsupported in v1)", NULL },
    { NULL }
};

/* --- password prompt ----------------------------------------------------- */

typedef struct {
    GMainLoop *loop;
    char      *password;
    gboolean   accepted;
} PromptCtx;

static void
on_response(G_GNUC_UNUSED AdwAlertDialog *dlg, const char *response, gpointer user_data)
{
    PromptCtx *ctx = user_data;
    if (g_strcmp0(response, "ok") == 0)
        ctx->accepted = TRUE;
    g_main_loop_quit(ctx->loop);
}

static void
on_entry_activate(GtkEntry *entry, gpointer user_data)
{
    PromptCtx *ctx = user_data;
    ctx->password = g_strdup(gtk_editable_get_text(GTK_EDITABLE(entry)));
    ctx->accepted = TRUE;
    g_main_loop_quit(ctx->loop);
}

static char *
prompt_for_password(const char *connection_name, const char *user)
{
    PromptCtx ctx = { .loop = g_main_loop_new(NULL, FALSE) };

    g_autofree char *heading = g_strdup_printf("Connect to %s",
                                               connection_name ? connection_name : "FortiVPN");
    g_autofree char *body = user
        ? g_strdup_printf("Enter the password for <b>%s</b>.", user)
        : g_strdup("Enter the VPN password.");

    AdwAlertDialog *dlg = ADW_ALERT_DIALOG(adw_alert_dialog_new(heading, NULL));
    adw_alert_dialog_set_body(dlg, body);
    adw_alert_dialog_set_body_use_markup(dlg, TRUE);
    adw_alert_dialog_add_responses(dlg,
                                   "cancel", "_Cancel",
                                   "ok",     "_Connect",
                                   NULL);
    adw_alert_dialog_set_default_response(dlg, "ok");
    adw_alert_dialog_set_close_response(dlg, "cancel");
    adw_alert_dialog_set_response_appearance(dlg, "ok", ADW_RESPONSE_SUGGESTED);

    GtkWidget *entry = gtk_password_entry_new();
    gtk_password_entry_set_show_peek_icon(GTK_PASSWORD_ENTRY(entry), TRUE);
    g_signal_connect(entry, "activate", G_CALLBACK(on_entry_activate), &ctx);
    adw_alert_dialog_set_extra_child(dlg, entry);

    g_signal_connect(dlg, "response", G_CALLBACK(on_response), &ctx);

    adw_dialog_present(ADW_DIALOG(dlg), NULL);
    gtk_widget_grab_focus(entry);

    g_main_loop_run(ctx.loop);

    if (ctx.accepted && !ctx.password)
        ctx.password = g_strdup(gtk_editable_get_text(GTK_EDITABLE(entry)));

    g_main_loop_unref(ctx.loop);
    return ctx.accepted ? ctx.password : NULL;
}

/* --- secret return on stdout -------------------------------------------- */

static void
emit_secret(const char *key, const char *value)
{
    if (!value)
        return;
    /* keyfile-ish protocol: name then value, both terminated with \n */
    fputs(key,   stdout); fputc('\n', stdout);
    fputs(value, stdout); fputc('\n', stdout);
}

static void
wait_for_quit(void)
{
    char buf[64];
    while (fgets(buf, sizeof buf, stdin)) {
        if (g_str_has_prefix(buf, "QUIT"))
            break;
    }
}

/* --- main --------------------------------------------------------------- */

int
main(int argc, char *argv[])
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GOptionContext) opt =
        g_option_context_new("- NetworkManager FortiVPN auth dialog");
    g_option_context_add_main_entries(opt, option_entries, NULL);
    if (!g_option_context_parse(opt, &argc, &argv, &error)) {
        g_printerr("%s\n", error->message);
        return EXIT_FAILURE;
    }

    if (opt_external_ui) {
        g_printerr("nm-openfortivpn-auth-dialog: --external-ui-mode is not implemented yet\n");
        return EXIT_FAILURE;
    }

    g_autoptr(GHashTable) data = NULL, secrets = NULL;
    if (!nm_vpn_service_plugin_read_vpn_details(0 /* stdin */, &data, &secrets)) {
        g_printerr("nm-openfortivpn-auth-dialog: failed to read VPN details from stdin\n");
        return EXIT_FAILURE;
    }

    const char *existing = g_hash_table_lookup(secrets, NM_OPENFORTIVPN_KEY_PASSWORD);
    const char *user     = g_hash_table_lookup(data,    NM_OPENFORTIVPN_KEY_USER);

    g_autofree char *password = NULL;
    if (existing && *existing && !opt_reprompt) {
        password = g_strdup(existing);
    } else if (opt_allow_interaction) {
        adw_init();
        password = prompt_for_password(opt_name, user);
        if (!password)
            return EXIT_FAILURE;   /* user cancelled */
    } else {
        /* No password and no permission to prompt -- nothing we can do. */
        g_printerr("nm-openfortivpn-auth-dialog: password missing and interaction not allowed\n");
        return EXIT_FAILURE;
    }

    emit_secret(NM_OPENFORTIVPN_KEY_PASSWORD, password);
    fputc('\n', stdout);
    fflush(stdout);

    wait_for_quit();
    return EXIT_SUCCESS;
}
