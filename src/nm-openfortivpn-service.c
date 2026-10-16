/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * nm-openfortivpn-service -- NetworkManager VPN service that drives openfortivpn.
 *
 * Skeleton scope:
 *   - subclass NMVpnServicePlugin to claim the D-Bus name
 *   - on Connect, spawn openfortivpn with config built from NMSettingVpn data
 *   - on tunnel-up (parsed from openfortivpn stdout), publish a stub IP4Config
 *   - on losing an established tunnel, report STARTING and restart
 *     openfortivpn with the session's password until the tunnel is back
 *   - on disconnect, login rejection or a failed first connect, transition to
 *     STOPPED; the process stays until openfortivpn has exited, killing it if
 *     SIGINT isn't enough
 *
 * The skeleton intentionally does NOT yet ship a pppd plugin; IP4Config push
 * here is best-effort and will need to be replaced for production use.
 */

#include "config.h"

#ifdef OPENFORTIVPN_TEST_PATH
#undef OPENFORTIVPN_PATH
#define OPENFORTIVPN_PATH OPENFORTIVPN_TEST_PATH
#endif

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <gio/gio.h>
#include <glib-unix.h>
#include <NetworkManager.h>

#include "nm-openfortivpn-service.h"
#include "nm-openfortivpn-service-defines.h"
#include "utils.h"

/* How long openfortivpn gets to log out and exit after SIGINT before SIGKILL.
 * Killing it closes pppd's pty, so pppd still hangs up and removes the link. */
#define CHILD_KILL_TIMEOUT_SEC 5

/* Wait before restarting openfortivpn after the tunnel is lost, doubled after
 * each attempt up to RECONNECT_DELAY_MAX_SEC. Back at the minimum once a
 * tunnel has stayed up for RECONNECT_DELAY_MAX_SEC, so one that drops again
 * right after each login keeps backing off. */
#ifndef RECONNECT_DELAY_MIN_SEC
#define RECONNECT_DELAY_MIN_SEC 5
#endif
#define RECONNECT_DELAY_MAX_SEC 60

/* --- plugin object ------------------------------------------------------- */

struct _NMOpenfortivpnPlugin {
    NMVpnServicePlugin parent;

    GSubprocess  *child;            /* openfortivpn subprocess, until it exits */
    GCancellable *cancellable;      /* aborts stdout reader on dispose */
    GDataInputStream *child_stdout; /* line-buffered reader for child stderr+stdout */
    gboolean      stopping;         /* disconnect requested; child told to exit */
    guint         kill_timeout_id;  /* SIGKILL fallback while stopping */
    gboolean      failing;          /* report `failure` when the disconnect runs */
    NMVpnPluginFailure failure;

    /* The session: kept from Connect so a lost tunnel can be restored without
     * asking NetworkManager for secrets again. Wiped when the session ends. */
    char        **argv;
    char         *password;
    gboolean      tunnel_up;        /* this openfortivpn reported the tunnel up */
    gboolean      established;      /* the tunnel came up once this session */
    guint         reconnect_id;     /* pending restart of openfortivpn */
    guint         reconnect_delay;  /* seconds before the next restart */
    gint64        up_since;         /* monotonic time the tunnel came up */
    char         *gateway;          /* configured FQDN, e.g. "vpn.moreh.dev" */
    char         *tundev;           /* from "Interface pppN is UP", e.g. "ppp1" */

    /* Parsed from openfortivpn's "Got addresses: [..], ns [..]" line. Used to
     * build the IP4Config we hand to NetworkManager when the tunnel is up. */
    char         *local_addr;       /* dotted-quad, e.g. "192.168.60.15" */
    char        **dns_list;         /* NULL-terminated dotted-quad strings   */
};

G_DEFINE_TYPE(NMOpenfortivpnPlugin, nm_openfortivpn_plugin, NM_TYPE_VPN_SERVICE_PLUGIN)

enum { SIGNAL_CHILD_EXITED, N_SIGNALS };
static guint signals[N_SIGNALS];

/* --- helpers ------------------------------------------------------------- */

static void
openfortivpn_fail(NMOpenfortivpnPlugin *self, NMVpnPluginFailure reason, const char *fmt, ...)
{
    g_autofree char *msg = NULL;
    va_list ap;

    va_start(ap, fmt);
    msg = g_strdup_vprintf(fmt, ap);
    va_end(ap);

    g_warning("%s", msg);
    /* openfortivpn_disconnect() reports the reason. Reporting it here, as
     * nm_vpn_service_plugin_failure() does, would let the base class follow it
     * with CONNECT_FAILED while STARTING, and NetworkManager keeps the last. */
    self->failing = TRUE;
    self->failure = reason;
    nm_vpn_service_plugin_disconnect(NM_VPN_SERVICE_PLUGIN(self), NULL);
}

/* openfortivpn's messages for a login the gateway refused, or one that needs
 * a second factor nobody is there to enter. Retrying won't get past either. */
static gboolean
is_login_rejection(const char *line)
{
    return strstr(line, "Could not authenticate") ||
           strstr(line, "authentication failed") ||
           strstr(line, "No OTP specified") ||
           strstr(line, "No token specified");
}

/* Build the argv passed to openfortivpn. Password is delivered separately
 * over the child's stdin so it doesn't leak into /proc/<pid>/cmdline. */
static char **
build_openfortivpn_argv(NMSettingVpn *s_vpn, GError **error)
{
    const char *gateway = nm_setting_vpn_get_data_item(s_vpn, NM_OPENFORTIVPN_KEY_GATEWAY);
    const char *port    = nm_setting_vpn_get_data_item(s_vpn, NM_OPENFORTIVPN_KEY_PORT);
    const char *user    = nm_setting_vpn_get_data_item(s_vpn, NM_OPENFORTIVPN_KEY_USER);
    const char *trusted = nm_setting_vpn_get_data_item(s_vpn, NM_OPENFORTIVPN_KEY_TRUSTED_CERT);
    const char *no_dtls = nm_setting_vpn_get_data_item(s_vpn, NM_OPENFORTIVPN_KEY_NO_DTLS);
    const char *insecure = nm_setting_vpn_get_data_item(s_vpn, NM_OPENFORTIVPN_KEY_INSECURE_SSL);

    if (!gateway || !*gateway) {
        g_set_error_literal(error, NM_VPN_PLUGIN_ERROR,
                            NM_VPN_PLUGIN_ERROR_BAD_ARGUMENTS,
                            "missing required '" NM_OPENFORTIVPN_KEY_GATEWAY "'");
        return NULL;
    }
    if (!user || !*user) {
        g_set_error_literal(error, NM_VPN_PLUGIN_ERROR,
                            NM_VPN_PLUGIN_ERROR_BAD_ARGUMENTS,
                            "missing required '" NM_OPENFORTIVPN_KEY_USER "'");
        return NULL;
    }

    g_autoptr(GPtrArray) argv = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(argv, g_strdup(OPENFORTIVPN_PATH));

    if (port && *port)
        g_ptr_array_add(argv, g_strdup_printf("%s:%s", gateway, port));
    else
        g_ptr_array_add(argv, g_strdup(gateway));

    g_ptr_array_add(argv, g_strdup("--username"));
    g_ptr_array_add(argv, g_strdup(user));

    /* Read password from stdin so it isn't in argv. */
    g_ptr_array_add(argv, g_strdup("--pppd-log=/dev/null"));
    g_ptr_array_add(argv, g_strdup("--persistent=0"));

    if (trusted && *trusted) {
        g_ptr_array_add(argv, g_strdup("--trusted-cert"));
        g_ptr_array_add(argv, g_strdup(trusted));
    }
    if (nm_openfortivpn_str_is_yes(no_dtls))
        g_ptr_array_add(argv, g_strdup("--no-dtls"));
    if (nm_openfortivpn_str_is_yes(insecure))
        g_ptr_array_add(argv, g_strdup("--insecure-ssl"));

    g_ptr_array_add(argv, NULL);
    return (char **) g_ptr_array_steal(argv, NULL);
}

/* Parse:
 *   "...Got addresses: [192.168.60.15], ns [1.249.213.155, 8.8.8.8]..."
 * The local PPP IP plus DNS list are all we need from openfortivpn's text
 * output; the rest of the route table is installed by openfortivpn itself
 * via /etc/ppp/ip-up.d. A future pppd plugin will replace this scrape. */
static void
parse_got_addresses(NMOpenfortivpnPlugin *self, const char *line)
{
    const char *p = strstr(line, "Got addresses: [");
    if (!p) return;
    p += strlen("Got addresses: [");
    const char *end = strchr(p, ']');
    if (!end) return;

    g_clear_pointer(&self->local_addr, g_free);
    self->local_addr = g_strndup(p, end - p);

    p = strstr(end, "ns [");
    if (!p) return;
    p += 4;
    end = strchr(p, ']');
    if (!end) return;

    g_autofree char *ns = g_strndup(p, end - p);
    g_strfreev(self->dns_list);
    self->dns_list = g_strsplit(ns, ", ", 0);
}

/* Parse "...Interface ppp1 is UP." -- openfortivpn picks the next free pppN,
 * so a second tunnel (or a leftover one) isn't on ppp0. */
static void
parse_interface_up(NMOpenfortivpnPlugin *self, const char *line)
{
    const char *p = strstr(line, "Interface ");
    if (!p) return;
    p += strlen("Interface ");
    const char *end = strstr(p, " is UP");
    if (!end || end == p) return;

    g_clear_pointer(&self->tundev, g_free);
    self->tundev = g_strndup(p, end - p);
}

/* Build and emit the config NetworkManager expects when the tunnel comes
 * up. All IPv4 addresses must be packed as uint32 in network byte order —
 * passing them as strings (as the earlier skeleton did) made NM silently
 * drop them and then time out with "no VPN gateway address received".
 *
 * The interface and gateway go in a separate Config, not just the IP4Config:
 * once NetworkManager has seen one IP4Config it reads later ones as IPv4
 * only, so after a reconnect it would keep configuring the old interface. */
static void
publish_config(NMOpenfortivpnPlugin *self)
{
    GVariantBuilder b;
    struct in_addr in;

    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);

    if (!self->tundev)
        g_warning("openfortivpn: tunnel device wasn't parsed; assuming ppp0");
    g_variant_builder_add(&b, "{sv}",
                          NM_VPN_PLUGIN_CONFIG_TUNDEV,
                          g_variant_new_string(self->tundev ? self->tundev : "ppp0"));
    g_variant_builder_add(&b, "{sv}",
                          NM_VPN_PLUGIN_CONFIG_HAS_IP4, g_variant_new_boolean(TRUE));

    /* Resolve the VPN endpoint FQDN to an IPv4 — NM uses this to install a
     * host route to the gateway through the original (non-VPN) interface so
     * the encrypted traffic doesn't loop back into the tunnel. */
    if (self->gateway && *self->gateway) {
        struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
        struct addrinfo *res = NULL;
        if (getaddrinfo(self->gateway, NULL, &hints, &res) == 0 && res) {
            guint32 ext = ((struct sockaddr_in *) res->ai_addr)->sin_addr.s_addr;
            g_variant_builder_add(&b, "{sv}",
                                  NM_VPN_PLUGIN_CONFIG_EXT_GATEWAY,
                                  g_variant_new_uint32(ext));
            freeaddrinfo(res);
        } else {
            g_warning("openfortivpn: could not resolve gateway '%s'", self->gateway);
        }
    }

    nm_vpn_service_plugin_set_config(NM_VPN_SERVICE_PLUGIN(self),
                                     g_variant_builder_end(&b));

    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);

    if (self->local_addr && inet_pton(AF_INET, self->local_addr, &in) == 1) {
        g_variant_builder_add(&b, "{sv}",
                              NM_VPN_PLUGIN_IP4_CONFIG_ADDRESS,
                              g_variant_new_uint32(in.s_addr));
        /* PPP is point-to-point; without route info from pppd, advertise a
         * /32 host route and let openfortivpn's ip-up scripts install the
         * subnet routes the gateway pushed. */
        g_variant_builder_add(&b, "{sv}",
                              NM_VPN_PLUGIN_IP4_CONFIG_PREFIX,
                              g_variant_new_uint32(32));
    } else {
        g_warning("openfortivpn: tunnel is up but local IP wasn't parsed; "
                  "NM may reject the activation");
    }

    if (self->dns_list && self->dns_list[0]) {
        GVariantBuilder dns_b;
        g_variant_builder_init(&dns_b, G_VARIANT_TYPE("au"));
        for (gsize i = 0; self->dns_list[i]; i++) {
            struct in_addr dns_in;
            if (inet_pton(AF_INET, self->dns_list[i], &dns_in) == 1)
                g_variant_builder_add(&dns_b, "u", dns_in.s_addr);
        }
        g_variant_builder_add(&b, "{sv}",
                              NM_VPN_PLUGIN_IP4_CONFIG_DNS,
                              g_variant_builder_end(&dns_b));
    }

    /* Don't snatch the default route — FortiGate typically pushes split-tunnel
     * routes for specific subnets, and openfortivpn installs them itself. */
    g_variant_builder_add(&b, "{sv}",
                          NM_VPN_PLUGIN_IP4_CONFIG_NEVER_DEFAULT,
                          g_variant_new_boolean(TRUE));

    nm_vpn_service_plugin_set_ip4_config(NM_VPN_SERVICE_PLUGIN(self),
                                        g_variant_builder_end(&b));
}

/* --- tunnel state -------------------------------------------------------- */

static void
on_tunnel_up(NMOpenfortivpnPlugin *self)
{
    if (self->established)
        g_message("openfortivpn: tunnel restored");
    self->tunnel_up = TRUE;
    self->established = TRUE;
    self->up_since = g_get_monotonic_time();
    publish_config(self);
}

/* The tunnel went down without anyone asking. NetworkManager reads STARTED
 * going back to STARTING as the VPN reconnecting, and reports it as
 * activating until the next config. Only report it once the interface is
 * gone: NetworkManager takes the next change to the interface's settings,
 * such as the one its removal causes, as the reconnect being done. */
static void
on_tunnel_lost(NMOpenfortivpnPlugin *self)
{
    if (!self->tunnel_up)
        return;

    self->tunnel_up = FALSE;
    if (g_get_monotonic_time() - self->up_since >= RECONNECT_DELAY_MAX_SEC * G_USEC_PER_SEC)
        self->reconnect_delay = RECONNECT_DELAY_MIN_SEC;
    g_message("openfortivpn: tunnel lost; reconnecting");
    g_object_set(self, NM_VPN_SERVICE_PLUGIN_STATE, NM_VPN_SERVICE_STATE_STARTING, NULL);
}

/* --- child stdout reader ------------------------------------------------- */

static void child_read_line_async(NMOpenfortivpnPlugin *self, GDataInputStream *stream);

static void
on_child_line(GObject *src, GAsyncResult *res, gpointer user_data)
{
    NMOpenfortivpnPlugin *self = NM_OPENFORTIVPN_PLUGIN(user_data);
    GDataInputStream *stream = G_DATA_INPUT_STREAM(src);
    g_autoptr(GError) error = NULL;
    gsize len = 0;
    g_autofree char *line =
        g_data_input_stream_read_line_finish_utf8(stream, res, &len, &error);

    if (g_cancellable_is_cancelled(self->cancellable))
        return;

    if (error) {
        if (stream == self->child_stdout && !self->stopping)
            openfortivpn_fail(self, NM_VPN_PLUGIN_FAILURE_CONNECT_FAILED,
                              "openfortivpn read error: %s", error->message);
        return;
    }
    if (!line) {
        /* EOF — child has closed its streams. State will follow via the
         * waitcheck callback. */
        return;
    }

    /* Echo every child line into the journal so we can see what openfortivpn
     * is doing during activation. Cheap and high-signal during development. */
    fprintf(stderr, "openfortivpn: %s\n", line);
    fflush(stderr);

    /* Keep draining while stopping so openfortivpn's teardown never blocks on
     * a full pipe, but its errors no longer mean the connection failed. The
     * same goes for whatever an earlier openfortivpn left in its pipe. */
    if (self->stopping || stream != self->child_stdout) {
        child_read_line_async(self, stream);
        return;
    }

    /* Capture the address line before the tunnel-up line so the IP4Config
     * we build has something to fill in. openfortivpn prints "Got addresses"
     * a few milliseconds before "Tunnel is up and running". */
    if (strstr(line, "Got addresses: [")) {
        parse_got_addresses(self, line);
    } else if (strstr(line, "Interface ") && strstr(line, " is UP")) {
        parse_interface_up(self, line);
    } else if (strstr(line, "Tunnel is up and running")) {
        on_tunnel_up(self);
    } else if (strstr(line, "Terminated pppd")) {
        /* pppd is gone and its interface with it; openfortivpn logs out and
         * exits next. */
        on_tunnel_lost(self);
    } else if (!self->tunnel_up && is_login_rejection(line)) {
        /* Once the tunnel is up, the same message is just how it ended. */
        openfortivpn_fail(self, NM_VPN_PLUGIN_FAILURE_LOGIN_FAILED,
                          "openfortivpn: gateway rejected the login: %s", line);
    } else if (!self->established && strstr(line, "ERROR:")) {
        /* The first connect reports errors at once. After that, a failed
         * attempt just exits and the next one follows. */
        openfortivpn_fail(self, NM_VPN_PLUGIN_FAILURE_CONNECT_FAILED, "%s", line);
    }

    child_read_line_async(self, stream);
}

static void
child_read_line_async(NMOpenfortivpnPlugin *self, GDataInputStream *stream)
{
    g_data_input_stream_read_line_async(stream,
                                        G_PRIORITY_DEFAULT,
                                        self->cancellable,
                                        on_child_line,
                                        self);
}

/* --- child lifecycle ----------------------------------------------------- */

static gboolean spawn_openfortivpn(NMOpenfortivpnPlugin *self, GError **error);
static void schedule_reconnect(NMOpenfortivpnPlugin *self);

static void
on_child_exited(GObject *src, GAsyncResult *res, gpointer user_data)
{
    g_autoptr(NMOpenfortivpnPlugin) self = NM_OPENFORTIVPN_PLUGIN(user_data);
    g_autoptr(GError) error = NULL;
    gboolean ok = g_subprocess_wait_check_finish(G_SUBPROCESS(src), res, &error);

    g_clear_handle_id(&self->kill_timeout_id, g_source_remove);
    g_clear_object(&self->child);

    if (self->stopping) {
        /* Asked to exit, or it's already been reported. */
    } else if (self->established) {
        /* The session was up, so it's still wanted: get the tunnel back. */
        if (!ok)
            g_message("openfortivpn exited: %s", error->message);
        on_tunnel_lost(self);
        schedule_reconnect(self);
    } else if (!ok) {
        /* The first connect didn't make it: let NetworkManager report it. */
        openfortivpn_fail(self, NM_VPN_PLUGIN_FAILURE_CONNECT_FAILED,
                          "openfortivpn exited: %s", error->message);
    } else {
        nm_vpn_service_plugin_disconnect(NM_VPN_SERVICE_PLUGIN(self), NULL);
    }

    g_signal_emit(self, signals[SIGNAL_CHILD_EXITED], 0);
}

static gboolean
on_reconnect(gpointer user_data)
{
    NMOpenfortivpnPlugin *self = NM_OPENFORTIVPN_PLUGIN(user_data);
    g_autoptr(GError) error = NULL;

    self->reconnect_id = 0;
    if (!spawn_openfortivpn(self, &error)) {
        g_warning("openfortivpn: could not restart: %s", error->message);
        schedule_reconnect(self);
    }
    return G_SOURCE_REMOVE;
}

static void
schedule_reconnect(NMOpenfortivpnPlugin *self)
{
    guint delay = self->reconnect_delay;

    g_message("openfortivpn: reconnecting in %u s", delay);
    self->reconnect_delay = MIN(delay * 2, RECONNECT_DELAY_MAX_SEC);
    self->reconnect_id = g_timeout_add_seconds(delay, on_reconnect, self);
}

static gboolean
on_kill_timeout(gpointer user_data)
{
    NMOpenfortivpnPlugin *self = NM_OPENFORTIVPN_PLUGIN(user_data);

    self->kill_timeout_id = 0;
    if (self->child) {
        g_warning("openfortivpn didn't exit %d s after SIGINT; killing it",
                  CHILD_KILL_TIMEOUT_SEC);
        g_subprocess_force_exit(self->child);
    }
    return G_SOURCE_REMOVE;
}

static void
forget_session(NMOpenfortivpnPlugin *self)
{
    if (self->password) {
        explicit_bzero(self->password, strlen(self->password));
        g_clear_pointer(&self->password, g_free);
    }
    g_clear_pointer(&self->argv, g_strfreev);
}

/* End the session: no more reconnects, and ask openfortivpn to log out and
 * exit. on_child_exited() reports when it has; if it's still there after
 * CHILD_KILL_TIMEOUT_SEC it is killed. */
static void
stop_child(NMOpenfortivpnPlugin *self)
{
    if (self->stopping)
        return;

    self->stopping = TRUE;
    g_clear_handle_id(&self->reconnect_id, g_source_remove);
    forget_session(self);
    if (!self->child)
        return;

    /* SIGINT lets openfortivpn unwind cleanly (logout, route removal). */
    g_subprocess_send_signal(self->child, SIGINT);
    self->kill_timeout_id = g_timeout_add_seconds(CHILD_KILL_TIMEOUT_SEC,
                                                  on_kill_timeout, self);
}

gboolean
nm_openfortivpn_plugin_has_child(NMOpenfortivpnPlugin *self)
{
    return self->child != NULL;
}

/* Start openfortivpn for the current session; on reconnects too. */
static gboolean
spawn_openfortivpn(NMOpenfortivpnPlugin *self, GError **error)
{
    char **argv = self->argv;
    g_autoptr(GSubprocessLauncher) l =
        g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDIN_PIPE |
                                  G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                  G_SUBPROCESS_FLAGS_STDERR_MERGE);

    /* Whatever the last openfortivpn reported is gone with its tunnel. */
    self->tunnel_up = FALSE;
    g_clear_pointer(&self->tundev,     g_free);
    g_clear_pointer(&self->local_addr, g_free);
    g_clear_pointer(&self->dns_list,   g_strfreev);

    /* Diagnostic: log the exact argv before spawning. fprintf(stderr) instead
     * of g_message because we're trying to determine whether glib's structured
     * logger is reaching journald at all. */
    fprintf(stderr, "nm-openfortivpn-service: spawning:");
    for (int i = 0; argv[i]; i++) fprintf(stderr, " %s", argv[i]);
    fputc('\n', stderr);
    fflush(stderr);

    self->child = g_subprocess_launcher_spawnv(l, (const char *const *) argv, error);
    if (!self->child) {
        fprintf(stderr, "nm-openfortivpn-service: spawnv failed: %s\n",
                error && *error ? (*error)->message : "(no error)");
        fflush(stderr);
        return FALSE;
    }
    fprintf(stderr, "nm-openfortivpn-service: openfortivpn started, pid=%s\n",
            g_subprocess_get_identifier(self->child));
    fflush(stderr);

    /* Feed the password on stdin, then close it. */
    GOutputStream *child_stdin = g_subprocess_get_stdin_pipe(self->child);
    if (child_stdin && self->password) {
        g_autofree char *pw_line = g_strdup_printf("%s\n", self->password);
        g_output_stream_write_all(child_stdin, pw_line, strlen(pw_line),
                                  NULL, NULL, NULL);
        explicit_bzero(pw_line, strlen(pw_line));
        g_output_stream_close(child_stdin, NULL, NULL);
    }

    GInputStream *child_out = g_subprocess_get_stdout_pipe(self->child);
    g_clear_object(&self->child_stdout);
    self->child_stdout = g_data_input_stream_new(child_out);
    child_read_line_async(self, self->child_stdout);

    /* Not cancellable: the exit is what tells us the tunnel is gone. */
    g_subprocess_wait_check_async(self->child,
                                  NULL,
                                  on_child_exited,
                                  g_object_ref(self));
    return TRUE;
}

/* --- NMVpnServicePlugin vfuncs ------------------------------------------ */

static gboolean
openfortivpn_connect(NMVpnServicePlugin *plugin, NMConnection *connection, GError **error)
{
    NMOpenfortivpnPlugin *self = NM_OPENFORTIVPN_PLUGIN(plugin);
    NMSettingVpn *s_vpn = nm_connection_get_setting_vpn(connection);
    if (!s_vpn) {
        g_set_error_literal(error, NM_VPN_PLUGIN_ERROR,
                            NM_VPN_PLUGIN_ERROR_BAD_ARGUMENTS,
                            "connection has no VPN setting");
        return FALSE;
    }

    g_clear_pointer(&self->gateway, g_free);
    self->gateway = g_strdup(nm_setting_vpn_get_data_item(s_vpn, NM_OPENFORTIVPN_KEY_GATEWAY));

    g_auto(GStrv) argv = build_openfortivpn_argv(s_vpn, error);
    if (!argv)
        return FALSE;

    const char *password = nm_setting_vpn_get_secret(s_vpn, NM_OPENFORTIVPN_KEY_PASSWORD);
    if (!password) {
        g_set_error_literal(error, NM_VPN_PLUGIN_ERROR,
                            NM_VPN_PLUGIN_ERROR_BAD_ARGUMENTS,
                            "no password supplied");
        return FALSE;
    }

    g_clear_object(&self->cancellable);
    self->cancellable = g_cancellable_new();

    forget_session(self);
    self->argv = g_steal_pointer(&argv);
    self->password = g_strdup(password);
    self->stopping = FALSE;
    self->failing = FALSE;
    self->established = FALSE;
    self->reconnect_delay = RECONNECT_DELAY_MIN_SEC;

    if (!spawn_openfortivpn(self, error)) {
        forget_session(self);
        return FALSE;
    }

    /* Returning TRUE puts the plugin into STARTING; the base class moves it
     * to STARTED when we publish an IP4Config. */
    return TRUE;
}

static gboolean
openfortivpn_need_secrets(G_GNUC_UNUSED NMVpnServicePlugin *plugin,
                     NMConnection *connection,
                     const char **setting_name,
                     G_GNUC_UNUSED GError **error)
{
    NMSettingVpn *s_vpn = nm_connection_get_setting_vpn(connection);
    if (!s_vpn ||
        !nm_setting_vpn_get_secret(s_vpn, NM_OPENFORTIVPN_KEY_PASSWORD)) {
        *setting_name = NM_SETTING_VPN_SETTING_NAME;
        return TRUE;
    }
    return FALSE;
}

static gboolean
openfortivpn_disconnect(NMVpnServicePlugin *plugin, G_GNUC_UNUSED GError **error)
{
    NMOpenfortivpnPlugin *self = NM_OPENFORTIVPN_PLUGIN(plugin);

    /* Runs after the base class has reported any failure of its own, so this
     * reason is the one NetworkManager shows. The base class's disconnect
     * does nothing while STOPPING. */
    if (self->failing) {
        self->failing = FALSE;
        nm_vpn_service_plugin_failure(plugin, self->failure);
    }
    stop_child(self);
    return TRUE;
}

/* --- object plumbing ----------------------------------------------------- */

static void
nm_openfortivpn_plugin_init(G_GNUC_UNUSED NMOpenfortivpnPlugin *self) { }

static void
nm_openfortivpn_plugin_dispose(GObject *obj)
{
    NMOpenfortivpnPlugin *self = NM_OPENFORTIVPN_PLUGIN(obj);

    if (self->cancellable)
        g_cancellable_cancel(self->cancellable);

    /* Last resort: main() waits for the child, so it should be gone here. */
    if (self->child)
        g_subprocess_force_exit(self->child);

    g_clear_handle_id(&self->kill_timeout_id, g_source_remove);
    g_clear_handle_id(&self->reconnect_id, g_source_remove);
    forget_session(self);
    g_clear_object(&self->child_stdout);
    g_clear_object(&self->child);
    g_clear_object(&self->cancellable);
    g_clear_pointer(&self->gateway,    g_free);
    g_clear_pointer(&self->tundev,     g_free);
    g_clear_pointer(&self->local_addr, g_free);
    g_clear_pointer(&self->dns_list,   g_strfreev);

    G_OBJECT_CLASS(nm_openfortivpn_plugin_parent_class)->dispose(obj);
}

static void
nm_openfortivpn_plugin_class_init(NMOpenfortivpnPluginClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    NMVpnServicePluginClass *vpn_class = NM_VPN_SERVICE_PLUGIN_CLASS(klass);

    gobject_class->dispose = nm_openfortivpn_plugin_dispose;

    /* Emitted once openfortivpn has exited, however it ended. */
    signals[SIGNAL_CHILD_EXITED] =
        g_signal_new("child-exited",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     0, NULL, NULL, NULL,
                     G_TYPE_NONE, 0);
    vpn_class->connect      = openfortivpn_connect;
    vpn_class->need_secrets = openfortivpn_need_secrets;
    vpn_class->disconnect   = openfortivpn_disconnect;
}

NMOpenfortivpnPlugin *
nm_openfortivpn_plugin_new(const char *bus_name, GError **error)
{
    return g_initable_new(NM_TYPE_OPENFORTIVPN_PLUGIN,
                          NULL, error,
                          NM_VPN_SERVICE_PLUGIN_DBUS_SERVICE_NAME, bus_name,
                          NULL);
}

/* --- entry point --------------------------------------------------------- */

static gboolean opt_persist = FALSE;
static gboolean opt_debug   = FALSE;
static char    *opt_bus_name = NULL;

typedef struct {
    GMainLoop             *loop;
    NMOpenfortivpnPlugin  *plugin;
    gboolean               stopped;   /* plugin reached STOPPED or was told to quit */
} RuntimeCtx;

static GOptionEntry option_entries[] = {
    { "persist",   0, 0, G_OPTION_ARG_NONE,   &opt_persist,
      "Don't quit after a single disconnect", NULL },
    { "debug",     0, 0, G_OPTION_ARG_NONE,   &opt_debug,
      "Enable verbose logging", NULL },
    { "bus-name",  0, 0, G_OPTION_ARG_STRING, &opt_bus_name,
      "D-Bus name to claim", "NAME" },
    { NULL }
};

/* Exit only once openfortivpn is gone too, so it's never left running
 * without the service that would stop it. */
static void
maybe_quit(RuntimeCtx *ctx)
{
    if (ctx->stopped && !nm_openfortivpn_plugin_has_child(ctx->plugin))
        g_main_loop_quit(ctx->loop);
}

static void
stop_and_quit(RuntimeCtx *ctx)
{
    openfortivpn_disconnect(NM_VPN_SERVICE_PLUGIN(ctx->plugin), NULL);
    ctx->stopped = TRUE;
    maybe_quit(ctx);
}

static gboolean
on_term(gpointer user_data)
{
    stop_and_quit(user_data);
    return G_SOURCE_REMOVE;
}

static void
on_nm_vanished(G_GNUC_UNUSED GDBusConnection *connection,
               const char      *name,
               gpointer         user_data)
{
    g_message("%s disappeared; stopping openfortivpn service", name);
    stop_and_quit(user_data);
}

static void
on_state_changed(G_GNUC_UNUSED NMVpnServicePlugin *plugin,
                 NMVpnServiceState state,
                 gpointer user_data)
{
    RuntimeCtx *ctx = user_data;

    if (state == NM_VPN_SERVICE_STATE_STOPPED) {
        ctx->stopped = TRUE;
        maybe_quit(ctx);
    }
}

static void
on_quit(G_GNUC_UNUSED NMVpnServicePlugin *plugin, gpointer user_data)
{
    stop_and_quit(user_data);
}

static void
on_child_exited_quit(G_GNUC_UNUSED NMOpenfortivpnPlugin *plugin, gpointer user_data)
{
    maybe_quit(user_data);
}

int
main(int argc, char *argv[])
{
    /* Startup marker — verifies our stderr is captured by whoever spawned us. */
    fprintf(stderr, "nm-openfortivpn-service: started, pid=%d\n", getpid());
    fflush(stderr);

    g_autoptr(GError) error = NULL;
    g_autoptr(GOptionContext) opt = g_option_context_new("- NetworkManager FortiVPN plugin");

    g_option_context_add_main_entries(opt, option_entries, NULL);
    if (!g_option_context_parse(opt, &argc, &argv, &error)) {
        g_printerr("%s\n", error->message);
        return EXIT_FAILURE;
    }

    if (opt_debug) {
        const char *cur = g_getenv("G_MESSAGES_DEBUG");
        g_setenv("G_MESSAGES_DEBUG",
                 cur && *cur ? cur : "all", TRUE);
    }

    g_autofree char *bus_name = opt_bus_name
        ? g_strdup(opt_bus_name)
        : g_strdup(NM_DBUS_SERVICE_OPENFORTIVPN);

    /* Declared before the plugin so it's freed after it: the plugin's
     * handlers use the loop until they're disconnected below. */
    g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);

    g_autoptr(NMOpenfortivpnPlugin) plugin = nm_openfortivpn_plugin_new(bus_name, &error);
    if (!plugin) {
        g_printerr("Failed to claim %s: %s\n", bus_name, error->message);
        return EXIT_FAILURE;
    }

    RuntimeCtx runtime = {
        .loop = loop,
        .plugin = plugin,
    };

    g_unix_signal_add(SIGTERM, on_term, &runtime);
    g_unix_signal_add(SIGINT,  on_term, &runtime);

    guint nm_watch_id = g_bus_watch_name(G_BUS_TYPE_SYSTEM,
                                         NM_DBUS_SERVICE,
                                         G_BUS_NAME_WATCHER_FLAGS_NONE,
                                         NULL,
                                         on_nm_vanished,
                                         &runtime,
                                         NULL);

    g_signal_connect(plugin, "child-exited", G_CALLBACK(on_child_exited_quit), &runtime);
    if (!opt_persist) {
        g_signal_connect(plugin, "quit", G_CALLBACK(on_quit), &runtime);
        g_signal_connect(plugin, "state-changed", G_CALLBACK(on_state_changed), &runtime);
    }

    g_main_loop_run(loop);
    g_bus_unwatch_name(nm_watch_id);

    /* The handlers point at the stack-allocated runtime; drop them before the
     * plugin's dispose emits state-changed on its way out. */
    g_signal_handlers_disconnect_by_data(plugin, &runtime);
    return EXIT_SUCCESS;
}
