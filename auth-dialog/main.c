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
#include <libsecret/secret.h>

#include "nm-openfortivpn-service-defines.h"

#define UI_KEYFILE_GROUP "VPN Plugin UI"
#define KEYRING_UUID_TAG "connection-uuid"
#define KEYRING_SETTING_TAG "setting-name"
#define KEYRING_KEY_TAG "setting-key"

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

/* --- user keyring lookup ------------------------------------------------ *
 *
 * Read-only. The secret agent (gnome-shell, nm-applet) owns the keyring: it
 * saves agent-owned secrets this dialog returns. Writing the keyring or the
 * connection from here makes the agent delete the saved password, because its
 * save deletes every item for the connection before writing what it was given. */

static const SecretSchema openfortivpn_secret_schema = {
    .name = "org.freedesktop.NetworkManager.Connection",
    .flags = SECRET_SCHEMA_DONT_MATCH_NAME,
    .attributes = {
        { KEYRING_UUID_TAG, SECRET_SCHEMA_ATTRIBUTE_STRING },
        { KEYRING_SETTING_TAG, SECRET_SCHEMA_ATTRIBUTE_STRING },
        { KEYRING_KEY_TAG, SECRET_SCHEMA_ATTRIBUTE_STRING },
        { NULL, 0 },
    },
    .reserved = 0,
    .reserved1 = NULL,
    .reserved2 = NULL,
    .reserved3 = NULL,
    .reserved4 = NULL,
    .reserved5 = NULL,
    .reserved6 = NULL,
    .reserved7 = NULL,
};

static char *
lookup_saved_password(const char *uuid)
{
    if (!uuid || !*uuid)
        return NULL;

    g_autoptr(GError) error = NULL;
    char *password = secret_password_lookup_sync(&openfortivpn_secret_schema,
                                                 NULL,
                                                 &error,
                                                 KEYRING_UUID_TAG, uuid,
                                                 KEYRING_SETTING_TAG, NM_SETTING_VPN_SETTING_NAME,
                                                 KEYRING_KEY_TAG, NM_OPENFORTIVPN_KEY_PASSWORD,
                                                 NULL);
    if (error)
        g_debug("failed to lookup saved password: %s", error->message);

    return password;
}

/* --- external UI mode ---------------------------------------------------- */

static void
emit_external_ui_request(const char *connection_name,
                         const char *user,
                         const char *existing_password,
                         gboolean allow_interaction)
{
    g_autoptr(GKeyFile) keyfile = g_key_file_new();
    g_autofree char *description = user
        ? g_strdup_printf("Enter the password for %s.", user)
        : g_strdup_printf("Enter the VPN password for %s.",
                          connection_name && *connection_name
                              ? connection_name
                              : "FortiVPN");
    g_autofree char *data = NULL;
    gsize length = 0;

    g_key_file_set_integer(keyfile, UI_KEYFILE_GROUP, "Version", 2);
    g_key_file_set_string(keyfile, UI_KEYFILE_GROUP, "Title", "VPN password required");
    g_key_file_set_string(keyfile, UI_KEYFILE_GROUP, "Description", description);

    g_key_file_set_string(keyfile, NM_OPENFORTIVPN_KEY_PASSWORD, "Value",
                          existing_password ? existing_password : "");
    g_key_file_set_string(keyfile, NM_OPENFORTIVPN_KEY_PASSWORD, "Label", "Password");
    g_key_file_set_boolean(keyfile, NM_OPENFORTIVPN_KEY_PASSWORD, "IsSecret", TRUE);
    g_key_file_set_boolean(keyfile, NM_OPENFORTIVPN_KEY_PASSWORD, "ShouldAsk",
                           allow_interaction && (!existing_password || opt_reprompt));
    g_key_file_set_boolean(keyfile, NM_OPENFORTIVPN_KEY_PASSWORD, "ForceEcho", FALSE);

    data = g_key_file_to_data(keyfile, &length, NULL);
    fwrite(data, 1, length, stdout);
    fflush(stdout);
}

/* --- password prompt ----------------------------------------------------- */

typedef struct {
    GMainLoop *loop;
    GtkWindow *window;
    char      *password;
    gboolean   accepted;
    gboolean   completed;
    GtkEditable *entry;
} PromptCtx;

static void
finish_prompt(PromptCtx *ctx, gboolean accepted)
{
    if (ctx->completed)
        return;
    ctx->completed = TRUE;
    ctx->accepted = accepted;
    if (accepted)
        ctx->password = g_strdup(gtk_editable_get_text(ctx->entry));
    if (ctx->window)
        gtk_window_destroy(ctx->window);
    g_main_loop_quit(ctx->loop);
}

static void
on_connect_clicked(G_GNUC_UNUSED GtkButton *button, gpointer user_data)
{
    finish_prompt(user_data, TRUE);
}

static void
on_cancel_clicked(G_GNUC_UNUSED GtkButton *button, gpointer user_data)
{
    finish_prompt(user_data, FALSE);
}

static void
on_window_close(G_GNUC_UNUSED GtkWindow *window, gpointer user_data)
{
    PromptCtx *ctx = user_data;
    finish_prompt(ctx, FALSE);
}

static void
on_entry_activate(G_GNUC_UNUSED GtkWidget *entry, gpointer user_data)
{
    finish_prompt(user_data, TRUE);
}

static char *
prompt_for_password(const char *connection_name,
                    const char *user)
{
    PromptCtx ctx = { .loop = g_main_loop_new(NULL, FALSE) };

    g_autofree char *heading = g_strdup_printf("Connect to %s",
                                               connection_name ? connection_name : "FortiVPN");
    g_autofree char *body = user
        ? g_strdup_printf("Enter the password for %s.", user)
        : g_strdup("Enter the VPN password.");

    GtkWidget *window = gtk_window_new();
    ctx.window = GTK_WINDOW(window);
    gtk_window_set_title(ctx.window, heading);
    gtk_window_set_modal(ctx.window, TRUE);
    gtk_window_set_resizable(ctx.window, FALSE);
    gtk_window_set_default_size(ctx.window, 360, -1);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_margin_top(box, 18);
    gtk_widget_set_margin_bottom(box, 18);
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);
    gtk_window_set_child(ctx.window, box);

    GtkWidget *title = gtk_label_new(heading);
    gtk_widget_add_css_class(title, "title-3");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    gtk_box_append(GTK_BOX(box), title);

    GtkWidget *label = gtk_label_new(body);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_box_append(GTK_BOX(box), label);

    GtkWidget *entry = gtk_password_entry_new();
    ctx.entry = GTK_EDITABLE(entry);
    gtk_password_entry_set_show_peek_icon(GTK_PASSWORD_ENTRY(entry), TRUE);
    g_signal_connect(entry, "activate", G_CALLBACK(on_entry_activate), &ctx);
    gtk_box_append(GTK_BOX(box), entry);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(box), actions);

    GtkWidget *cancel = gtk_button_new_with_mnemonic("_Cancel");
    g_signal_connect(cancel, "clicked", G_CALLBACK(on_cancel_clicked), &ctx);
    gtk_box_append(GTK_BOX(actions), cancel);

    GtkWidget *connect = gtk_button_new_with_mnemonic("_Connect");
    gtk_widget_add_css_class(connect, "suggested-action");
    g_signal_connect(connect, "clicked", G_CALLBACK(on_connect_clicked), &ctx);
    gtk_box_append(GTK_BOX(actions), connect);

    g_signal_connect(window, "destroy", G_CALLBACK(on_window_close), &ctx);

    gtk_window_present(ctx.window);
    gtk_widget_grab_focus(entry);

    g_main_loop_run(ctx.loop);

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

    g_autoptr(GHashTable) data = NULL, secrets = NULL;
    if (!nm_vpn_service_plugin_read_vpn_details(0 /* stdin */, &data, &secrets)) {
        g_printerr("nm-openfortivpn-auth-dialog: failed to read VPN details from stdin\n");
        return EXIT_FAILURE;
    }

    const char *existing = g_hash_table_lookup(secrets, NM_OPENFORTIVPN_KEY_PASSWORD);
    const char *user     = g_hash_table_lookup(data,    NM_OPENFORTIVPN_KEY_USER);
    NMSettingSecretFlags password_flags = NM_SETTING_SECRET_FLAG_NONE;

    if (!nm_vpn_service_plugin_get_secret_flags(data,
                                                NM_OPENFORTIVPN_KEY_PASSWORD,
                                                &password_flags))
        password_flags = NM_SETTING_SECRET_FLAG_AGENT_OWNED;

    if (opt_external_ui) {
        g_autofree char *saved_password = NULL;
        if (!existing && !opt_reprompt &&
            !(password_flags & NM_SETTING_SECRET_FLAG_NOT_SAVED))
            saved_password = lookup_saved_password(opt_uuid);

        emit_external_ui_request(opt_name,
                                 user,
                                 existing ? existing : saved_password,
                                 opt_allow_interaction);
        return EXIT_SUCCESS;
    }

    g_autofree char *password = NULL;
    if (existing && *existing && !opt_reprompt)
        password = g_strdup(existing);
    else if (!opt_reprompt && !(password_flags & NM_SETTING_SECRET_FLAG_NOT_SAVED))
        password = lookup_saved_password(opt_uuid);

    if (!password && opt_allow_interaction) {
        adw_init();
        password = prompt_for_password(opt_name, user);
        if (!password)
            return EXIT_FAILURE;   /* user cancelled */
    }

    if (!password) {
        /* No password and no permission to prompt -- nothing we can do. */
        g_printerr("nm-openfortivpn-auth-dialog: password missing and interaction not allowed\n");
        return EXIT_FAILURE;
    }

    emit_secret(NM_OPENFORTIVPN_KEY_PASSWORD, password);
    fputs("\n\n", stdout);
    fflush(stdout);

    wait_for_quit();

    return EXIT_SUCCESS;
}
