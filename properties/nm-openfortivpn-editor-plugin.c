/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The connection-editor plugin entry point. nm-connection-editor dlopens this
 * shared module and calls nm_vpn_editor_plugin_factory() to instantiate the
 * GObject that implements NMVpnEditorPluginInterface. */

#include "config.h"

#include <NetworkManager.h>

#include "nm-openfortivpn-editor-plugin.h"
#include "nm-openfortivpn-editor.h"
#include "nm-openfortivpn-service-defines.h"

static void editor_plugin_iface_init(NMVpnEditorPluginInterface *iface);

struct _NMOpenfortivpnEditorPlugin {
    GObject parent;
};

G_DEFINE_TYPE_WITH_CODE(NMOpenfortivpnEditorPlugin, nm_openfortivpn_editor_plugin, G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE(NM_TYPE_VPN_EDITOR_PLUGIN,
                                              editor_plugin_iface_init))

enum {
    PROP_0,
    PROP_NAME,
    PROP_DESC,
    PROP_SERVICE,
};

/* --- iface impl --------------------------------------------------------- */

static NMVpnEditor *
get_editor(G_GNUC_UNUSED NMVpnEditorPlugin *iface, NMConnection *connection, GError **error)
{
    return nm_openfortivpn_editor_new(connection, error);
}

static NMVpnEditorPluginCapability
get_capabilities(G_GNUC_UNUSED NMVpnEditorPlugin *iface)
{
    /* No import/export of .conf files in v1. */
    return NM_VPN_EDITOR_PLUGIN_CAPABILITY_NONE;
}

static void
editor_plugin_iface_init(NMVpnEditorPluginInterface *iface)
{
    iface->get_editor      = get_editor;
    iface->get_capabilities = get_capabilities;
}

/* --- GObject plumbing --------------------------------------------------- */

static void
get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    switch (prop_id) {
    case PROP_NAME:    g_value_set_string(value, "openfortivpn"); break;
    case PROP_DESC:    g_value_set_string(value,
                          "Fortinet SSL VPN (via openfortivpn)"); break;
    case PROP_SERVICE: g_value_set_string(value, NM_DBUS_SERVICE_OPENFORTIVPN); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
}

static void
nm_openfortivpn_editor_plugin_init(G_GNUC_UNUSED NMOpenfortivpnEditorPlugin *self) { }

static void
nm_openfortivpn_editor_plugin_class_init(NMOpenfortivpnEditorPluginClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    gobject_class->get_property = get_property;

    g_object_class_override_property(gobject_class, PROP_NAME,    NM_VPN_EDITOR_PLUGIN_NAME);
    g_object_class_override_property(gobject_class, PROP_DESC,    NM_VPN_EDITOR_PLUGIN_DESCRIPTION);
    g_object_class_override_property(gobject_class, PROP_SERVICE, NM_VPN_EDITOR_PLUGIN_SERVICE);
}

/* --- factory (exported entry point) ------------------------------------- */

G_MODULE_EXPORT NMVpnEditorPlugin *
nm_vpn_editor_plugin_factory(G_GNUC_UNUSED GError **error)
{
    return NM_VPN_EDITOR_PLUGIN(
        g_object_new(NM_TYPE_OPENFORTIVPN_EDITOR_PLUGIN, NULL));
}
