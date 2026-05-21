/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NM_OPENFORTIVPN_EDITOR_H
#define NM_OPENFORTIVPN_EDITOR_H

#include <glib-object.h>
#include <NetworkManager.h>

G_BEGIN_DECLS

#define NM_TYPE_OPENFORTIVPN_EDITOR  (nm_openfortivpn_editor_get_type())
G_DECLARE_FINAL_TYPE(NMOpenfortivpnEditor, nm_openfortivpn_editor,
                    NM, OPENFORTIVPN_EDITOR, GObject)

NMVpnEditor *nm_openfortivpn_editor_new(NMConnection *connection, GError **error);

G_END_DECLS

#endif /* NM_OPENFORTIVPN_EDITOR_H */
