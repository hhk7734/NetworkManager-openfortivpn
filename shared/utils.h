/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NM_OPENFORTIVPN_UTILS_H
#define NM_OPENFORTIVPN_UTILS_H

#include <glib.h>

static inline gboolean
nm_openfortivpn_str_is_yes(const char *s)
{
    return s && (g_ascii_strcasecmp(s, "yes") == 0 ||
                 g_ascii_strcasecmp(s, "true") == 0 ||
                 g_strcmp0(s, "1") == 0);
}

#endif /* NM_OPENFORTIVPN_UTILS_H */
