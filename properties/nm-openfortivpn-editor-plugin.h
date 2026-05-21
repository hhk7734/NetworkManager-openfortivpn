/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NM_OPENFORTIVPN_EDITOR_PLUGIN_H
#define NM_OPENFORTIVPN_EDITOR_PLUGIN_H

#include <glib-object.h>
#include <NetworkManager.h>

G_BEGIN_DECLS

#define NM_TYPE_OPENFORTIVPN_EDITOR_PLUGIN  (nm_openfortivpn_editor_plugin_get_type())
G_DECLARE_FINAL_TYPE(NMOpenfortivpnEditorPlugin, nm_openfortivpn_editor_plugin,
                    NM, OPENFORTIVPN_EDITOR_PLUGIN, GObject)

G_END_DECLS

#endif /* NM_OPENFORTIVPN_EDITOR_PLUGIN_H */
