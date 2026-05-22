/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * GTK4 form embedded in nm-connection-editor when editing a FortiVPN
 * connection. Widgets are loaded from nm-openfortivpn-dialog.ui via GResource. */

#include "config.h"

#include <gtk/gtk.h>
#include <NetworkManager.h>

#include "nm-openfortivpn-editor.h"
#include "nm-openfortivpn-service-defines.h"

static void editor_iface_init(NMVpnEditorInterface *iface);

struct _NMOpenfortivpnEditor {
    GObject parent;

    GtkWidget   *widget;          /* the top-level GtkBox shown in the editor */
    GtkBuilder  *builder;

    GtkEditable *gateway_entry;
    GtkEditable *port_entry;
    GtkEditable *user_entry;
    GtkSwitch   *save_password_switch;
    GtkEditable *trusted_cert_entry;
    GtkSwitch   *insecure_switch;
};

G_DEFINE_TYPE_WITH_CODE(NMOpenfortivpnEditor, nm_openfortivpn_editor, G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE(NM_TYPE_VPN_EDITOR, editor_iface_init))

/* --- helpers ------------------------------------------------------------ */

static const char *
entry_text(GtkEditable *e)
{
    return gtk_editable_get_text(e);
}

static void
load_from_connection(NMOpenfortivpnEditor *self, NMConnection *connection)
{
    NMSettingVpn *s_vpn = connection ? nm_connection_get_setting_vpn(connection) : NULL;
    if (!s_vpn)
        return;

    const char *v;
#define LOAD_ENTRY(field, key) \
    do { v = nm_setting_vpn_get_data_item(s_vpn, (key)); \
         if (v) gtk_editable_set_text(self->field, v); } while (0)

    LOAD_ENTRY(gateway_entry,       NM_OPENFORTIVPN_KEY_GATEWAY);
    LOAD_ENTRY(port_entry,          NM_OPENFORTIVPN_KEY_PORT);
    LOAD_ENTRY(user_entry,          NM_OPENFORTIVPN_KEY_USER);
    LOAD_ENTRY(trusted_cert_entry,  NM_OPENFORTIVPN_KEY_TRUSTED_CERT);
#undef LOAD_ENTRY

    v = nm_setting_vpn_get_data_item(s_vpn, NM_OPENFORTIVPN_KEY_INSECURE_SSL);
    gtk_switch_set_active(self->insecure_switch,
                          v && (g_ascii_strcasecmp(v, "yes") == 0));

    NMSettingSecretFlags flags = NM_SETTING_SECRET_FLAG_AGENT_OWNED;
    nm_setting_get_secret_flags(NM_SETTING(s_vpn),
                                NM_OPENFORTIVPN_KEY_PASSWORD,
                                &flags,
                                NULL);
    gtk_switch_set_active(self->save_password_switch,
                          !(flags & NM_SETTING_SECRET_FLAG_NOT_SAVED));
}

static void
on_changed(G_GNUC_UNUSED GtkWidget *w, gpointer user_data)
{
    g_signal_emit_by_name(G_OBJECT(user_data), "changed");
}

static void
on_notify_changed(G_GNUC_UNUSED GObject *obj,
                  G_GNUC_UNUSED GParamSpec *pspec,
                  gpointer user_data)
{
    g_signal_emit_by_name(G_OBJECT(user_data), "changed");
}

/* --- iface impl --------------------------------------------------------- */

static GObject *
get_widget(NMVpnEditor *iface)
{
    NMOpenfortivpnEditor *self = NM_OPENFORTIVPN_EDITOR(iface);
    return G_OBJECT(self->widget);
}

static gboolean
update_connection(NMVpnEditor *iface, NMConnection *connection, G_GNUC_UNUSED GError **error)
{
    NMOpenfortivpnEditor *self = NM_OPENFORTIVPN_EDITOR(iface);

    g_autoptr(NMSettingVpn) s_vpn = NM_SETTING_VPN(nm_setting_vpn_new());
    g_object_set(s_vpn, NM_SETTING_VPN_SERVICE_TYPE, NM_DBUS_SERVICE_OPENFORTIVPN, NULL);

#define SET_IF(key, str) \
    do { const char *_v = (str); if (_v && *_v) nm_setting_vpn_add_data_item(s_vpn, (key), _v); } while (0)

    SET_IF(NM_OPENFORTIVPN_KEY_GATEWAY,      entry_text(self->gateway_entry));
    SET_IF(NM_OPENFORTIVPN_KEY_PORT,         entry_text(self->port_entry));
    SET_IF(NM_OPENFORTIVPN_KEY_USER,         entry_text(self->user_entry));
    SET_IF(NM_OPENFORTIVPN_KEY_TRUSTED_CERT, entry_text(self->trusted_cert_entry));
#undef SET_IF

    if (gtk_switch_get_active(self->insecure_switch))
        nm_setting_vpn_add_data_item(s_vpn, NM_OPENFORTIVPN_KEY_INSECURE_SSL, "yes");

    nm_setting_set_secret_flags(NM_SETTING(s_vpn),
                                NM_OPENFORTIVPN_KEY_PASSWORD,
                                gtk_switch_get_active(self->save_password_switch)
                                    ? NM_SETTING_SECRET_FLAG_AGENT_OWNED
                                    : NM_SETTING_SECRET_FLAG_NOT_SAVED,
                                NULL);

    nm_connection_add_setting(connection, NM_SETTING(g_steal_pointer(&s_vpn)));
    return TRUE;
}

static void
editor_iface_init(NMVpnEditorInterface *iface)
{
    iface->get_widget        = get_widget;
    iface->update_connection = update_connection;
}

/* --- construction ------------------------------------------------------- */

NMVpnEditor *
nm_openfortivpn_editor_new(NMConnection *connection, GError **error)
{
    NMOpenfortivpnEditor *self = g_object_new(NM_TYPE_OPENFORTIVPN_EDITOR, NULL);

    self->builder = gtk_builder_new_from_resource(
        "/org/freedesktop/NetworkManager/openfortivpn/nm-openfortivpn-dialog.ui");

    self->widget               = GTK_WIDGET  (gtk_builder_get_object(self->builder, "openfortivpn-box"));
    self->gateway_entry        = GTK_EDITABLE(gtk_builder_get_object(self->builder, "gateway-entry"));
    self->port_entry           = GTK_EDITABLE(gtk_builder_get_object(self->builder, "port-entry"));
    self->user_entry           = GTK_EDITABLE(gtk_builder_get_object(self->builder, "user-entry"));
    self->save_password_switch = GTK_SWITCH  (gtk_builder_get_object(self->builder, "save-password-switch"));
    self->trusted_cert_entry   = GTK_EDITABLE(gtk_builder_get_object(self->builder, "trusted-cert-entry"));
    self->insecure_switch      = GTK_SWITCH  (gtk_builder_get_object(self->builder, "insecure-switch"));

    if (!self->widget) {
        g_set_error_literal(error, NM_CONNECTION_ERROR,
                            NM_CONNECTION_ERROR_FAILED,
                            "could not load nm-openfortivpn-dialog.ui from GResource");
        g_object_unref(self);
        return NULL;
    }

    g_object_ref_sink(self->widget);

    load_from_connection(self, connection);

    g_signal_connect(self->gateway_entry,        "changed", G_CALLBACK(on_changed), self);
    g_signal_connect(self->port_entry,           "changed", G_CALLBACK(on_changed), self);
    g_signal_connect(self->user_entry,           "changed", G_CALLBACK(on_changed), self);
    g_signal_connect(self->save_password_switch, "notify::active",
                     G_CALLBACK(on_notify_changed), self);
    g_signal_connect(self->trusted_cert_entry,   "changed", G_CALLBACK(on_changed), self);
    g_signal_connect(self->insecure_switch,      "notify::active",
                     G_CALLBACK(on_notify_changed), self);

    return NM_VPN_EDITOR(self);
}

static void
nm_openfortivpn_editor_init(G_GNUC_UNUSED NMOpenfortivpnEditor *self) { }

static void
nm_openfortivpn_editor_dispose(GObject *obj)
{
    NMOpenfortivpnEditor *self = NM_OPENFORTIVPN_EDITOR(obj);
    g_clear_object(&self->builder);
    g_clear_object(&self->widget);
    G_OBJECT_CLASS(nm_openfortivpn_editor_parent_class)->dispose(obj);
}

static void
nm_openfortivpn_editor_class_init(NMOpenfortivpnEditorClass *klass)
{
    G_OBJECT_CLASS(klass)->dispose = nm_openfortivpn_editor_dispose;
}
