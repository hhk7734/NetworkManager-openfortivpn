/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NM_OPENFORTIVPN_SERVICE_H
#define NM_OPENFORTIVPN_SERVICE_H

#include <glib-object.h>
#include <NetworkManager.h>

G_BEGIN_DECLS

#define NM_TYPE_OPENFORTIVPN_PLUGIN  (nm_openfortivpn_plugin_get_type())
G_DECLARE_FINAL_TYPE(NMOpenfortivpnPlugin, nm_openfortivpn_plugin,
                    NM, OPENFORTIVPN_PLUGIN, NMVpnServicePlugin)

NMOpenfortivpnPlugin *nm_openfortivpn_plugin_new(const char *bus_name, GError **error);
gboolean nm_openfortivpn_plugin_has_child(NMOpenfortivpnPlugin *self);

G_END_DECLS

#endif /* NM_OPENFORTIVPN_SERVICE_H */
