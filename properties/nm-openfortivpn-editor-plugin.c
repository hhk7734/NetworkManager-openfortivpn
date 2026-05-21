/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Lightweight libnm plugin entry point. NetworkManager clients may load this
 * during VPN plugin discovery, so it must not link GTK or construct UI.
 */

#include "config.h"

#include <NetworkManager.h>

#include "nm-openfortivpn-editor-plugin.h"
#include "nm-openfortivpn-service-defines.h"

#define OPENFORTIVPN_PROPERTIES_PLUGIN "libnm-openfortivpn-properties.so"

static void editor_plugin_iface_init(NMVpnEditorPluginInterface *iface);

struct _NMOpenfortivpnEditorPlugin {
    GObject parent;

    NMVpnEditorPlugin *properties_plugin;
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

static const char *
get_properties_plugin_name(NMVpnEditorPlugin *iface)
{
    NMVpnPluginInfo *plugin_info = nm_vpn_editor_plugin_get_plugin_info(iface);
    const char *name = NULL;

    if (plugin_info) {
        name = nm_vpn_plugin_info_lookup_property(plugin_info,
                                                  NM_VPN_PLUGIN_INFO_KF_GROUP_GNOME,
                                                  "properties");
    }

    return (name && *name) ? name : OPENFORTIVPN_PROPERTIES_PLUGIN;
}

static char *
normalize_plugin_filename(const char *name)
{
    if (g_str_has_suffix(name, ".so"))
        return g_strdup(name);

    return g_strconcat(name, ".so", NULL);
}

static NMVpnEditorPlugin *
load_properties_plugin(NMOpenfortivpnEditorPlugin *self,
                       NMVpnEditorPlugin *iface,
                       GError **error)
{
    if (!self->properties_plugin) {
        g_autofree char *plugin = normalize_plugin_filename(get_properties_plugin_name(iface));
        int check_owner = g_path_is_absolute(plugin) ? -1 : 0;

        self->properties_plugin =
            nm_vpn_editor_plugin_load_from_file(plugin,
                                                NM_DBUS_SERVICE_OPENFORTIVPN,
                                                check_owner,
                                                NULL,
                                                NULL,
                                                error);
    }

    return self->properties_plugin;
}

/* --- iface impl --------------------------------------------------------- */

static NMVpnEditor *
get_editor(NMVpnEditorPlugin *iface, NMConnection *connection, GError **error)
{
    NMOpenfortivpnEditorPlugin *self = NM_OPENFORTIVPN_EDITOR_PLUGIN(iface);
    NMVpnEditorPlugin *properties_plugin = load_properties_plugin(self, iface, error);

    if (!properties_plugin)
        return NULL;

    return nm_vpn_editor_plugin_get_editor(properties_plugin, connection, error);
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
    iface->get_editor       = get_editor;
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
nm_openfortivpn_editor_plugin_dispose(GObject *obj)
{
    NMOpenfortivpnEditorPlugin *self = NM_OPENFORTIVPN_EDITOR_PLUGIN(obj);

    g_clear_object(&self->properties_plugin);
    G_OBJECT_CLASS(nm_openfortivpn_editor_plugin_parent_class)->dispose(obj);
}

static void
nm_openfortivpn_editor_plugin_class_init(NMOpenfortivpnEditorPluginClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    gobject_class->get_property = get_property;
    gobject_class->dispose = nm_openfortivpn_editor_plugin_dispose;

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
