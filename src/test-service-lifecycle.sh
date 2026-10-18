#!/bin/bash
# Drives a test build of nm-openfortivpn-service (spawning test-fake-openfortivpn)
# over the system bus. Owning the service's bus name needs root, so this runs
# only when NM_OPENFORTIVPN_ROOT_TESTS=1 and passwordless sudo is available.
set -u

service=$1
kill_timeout=${2:-5}

if [ "${NM_OPENFORTIVPN_ROOT_TESTS:-0}" != 1 ] || ! sudo -n true 2>/dev/null; then
  echo "skipping: set NM_OPENFORTIVPN_ROOT_TESTS=1 with passwordless sudo"
  exit 77
fi

tmpdir=$(mktemp -d)
failures=0
svc_pid=
monitor_pid=

cleanup() {
  [ -n "$monitor_pid" ] && kill "$monitor_pid" 2>/dev/null
  for pid in $(cat "$tmpdir/all-pids" 2>/dev/null) $(fake_pids); do
    sudo -n kill -9 "$pid" 2>/dev/null
  done
  rm -rf "$tmpdir"
}
trap cleanup EXIT

fake_pids() { sed -n 's/^start //p' "$tmpdir/fake.log" 2>/dev/null; }
last_fake() { fake_pids | tail -n1; }
starts() { fake_pids | wc -l; }
alive() { [ -d "/proc/$1" ]; }
tunnels_up() { grep -c 'Tunnel is up' "$tmpdir/service.log"; }
starts_reach() { [ "$(starts)" -ge "$1" ]; }
tunnels_reach() { [ "$(tunnels_up)" -ge "$1" ]; }

# What the service told NetworkManager, one per line: "StateChanged N",
# "Failure N", "Config" or "Ip4Config". States: 3 starting, 4 started,
# 6 stopped. Failures: 0 login failed, 1 connect failed.
events() {
  sed -n -e 's/.*VPN\.Plugin\.\(StateChanged\|Failure\) (uint32 \([0-9]*\),)$/\1 \2/p' \
         -e 's/.*VPN\.Plugin\.\(Config\|Ip4Config\) .*/\1/p' "$tmpdir/monitor.log"
}
states() { events | grep '^StateChanged' | cut -d' ' -f2 | paste -sd' '; }
last_failure() { events | grep '^Failure' | tail -n1; }

# start_gaps -- seconds between consecutive openfortivpn starts
start_gaps() {
  awk '$1 == "time" { if (prev) printf "%.1f\n", $3 - prev; prev = $3 }' "$tmpdir/fake.log"
}

# wait_until SECONDS COMMAND... -- succeed once COMMAND does
wait_until() {
  local deadline=$(( $(date +%s%N) + $1 * 1000000000 )); shift
  while [ "$(date +%s%N)" -lt "$deadline" ]; do
    "$@" && return 0
    sleep 0.1
  done
  return 1
}

# set_mode MODE -- how the next openfortivpn run goes (see test-fake-openfortivpn)
set_mode() { echo "$1" > "$tmpdir/mode"; }

# drop_tunnel -- the gateway ends the session of the running openfortivpn
drop_tunnel() { sudo -n kill -USR1 "$(last_fake)"; }

fail() { echo "FAIL: $1"; failures=$((failures + 1)); }
pass() { echo "ok: $1"; }

# wait_gone SECONDS PID... -- succeed once every PID has exited
wait_gone() {
  local deadline=$(( $(date +%s%N) + $1 * 1000000000 )); shift
  while [ "$(date +%s%N)" -lt "$deadline" ]; do
    local left=0
    for pid in "$@"; do alive "$pid" && left=1; done
    [ "$left" = 0 ] && return 0
    sleep 0.1
  done
  return 1
}

plugin_call() {
  sudo -n gdbus call --system --dest "$bus" \
    --object-path /org/freedesktop/NetworkManager/VPN/Plugin \
    --method "org.freedesktop.NetworkManager.VPN.Plugin.$1" "${@:2}" >/dev/null
}

# start_connected [ENV=VALUE...] -- start the service and connect it, to
# $gateway (default 192.0.2.1). With $hosts set, the service sees that file
# as /etc/hosts.
start_connected() {
  local wrap=()
  [ -n "${hosts:-}" ] &&
    wrap=(unshare --mount sh -c 'mount --bind "$0" /etc/hosts && exec "$@"' "$hosts")
  # Remember every earlier service and fake so cleanup can kill them all.
  echo $(fake_pids) $svc_pid >> "$tmpdir/all-pids"
  : > "$tmpdir/fake.log"
  : > "$tmpdir/monitor.log"
  set_mode "${initial_mode:-up}"
  bus=org.freedesktop.NetworkManager.openfortivpn.Test$$_$RANDOM
  gdbus monitor --system --dest "$bus" > "$tmpdir/monitor.log" 2>&1 &
  monitor_pid=$!
  sudo -n env FAKE_LOG="$tmpdir/fake.log" FAKE_MODE_FILE="$tmpdir/mode" "$@" \
    "${wrap[@]}" "$service" --bus-name "$bus" \
    > "$tmpdir/service.log" 2>&1 &
  for _ in $(seq 50); do
    svc_pid=$(pgrep -f "^$service --bus-name $bus\$")
    [ -n "$svc_pid" ] && gdbus introspect --system --dest "$bus" \
      --object-path /org/freedesktop/NetworkManager/VPN/Plugin >/dev/null 2>&1 && break
    sleep 0.1
  done
  plugin_call Connect "{'connection': {'id': <'test'>, 'uuid': <'00000000-0000-0000-0000-000000000001'>, 'type': <'vpn'>},
    'vpn': {'service-type': <'org.freedesktop.NetworkManager.openfortivpn'>,
            'data': <{'gateway': '${gateway:-192.0.2.1}', 'user': 'test'}>,
            'secrets': <{'password': 'secret'}>}}"
  for _ in $(seq 50); do
    grep -q 'Tunnel is up\|rejected the login' "$tmpdir/service.log" && break
    sleep 0.1
  done
  sleep 0.3
  fake=$(fake_pids)
}

stop_monitor() { kill "$monitor_pid" 2>/dev/null; wait "$monitor_pid" 2>/dev/null; monitor_pid=; }

# 1. The published config names the interface openfortivpn brought up.
start_connected FAKE_TUNDEV=ppp7
sleep 0.5
stop_monitor
if grep -q "'tundev': <'ppp7'>" "$tmpdir/monitor.log"; then
  pass "Ip4Config reports tundev ppp7"
else
  fail "Ip4Config does not report tundev ppp7: $(grep -o "'tundev': <'[^']*'>" "$tmpdir/monitor.log")"
fi

# 2. Disconnect stops openfortivpn, and the service outlives it.
plugin_call Disconnect
if wait_gone 3 "$fake" && grep -q 'got INT' "$tmpdir/fake.log"; then
  pass "disconnect stops openfortivpn with SIGINT"
else
  fail "openfortivpn still running after disconnect"
fi
wait_gone 3 "$svc_pid" && pass "service exits after disconnect" \
                       || fail "service still running after disconnect"

# 3. A child that ignores SIGINT is killed instead of orphaned.
start_connected FAKE_STUBBORN=1
stop_monitor
plugin_call Disconnect
if alive "$svc_pid" && alive "$fake"; then
  pass "service waits while openfortivpn is still running"
else
  fail "service exited while openfortivpn was still running"
fi
wait_gone $((kill_timeout + 3)) "$fake" && pass "stuck openfortivpn is killed" \
                                        || fail "stuck openfortivpn left running"
wait_gone 3 "$svc_pid" && pass "service exits after killing openfortivpn" \
                       || fail "service still running after killing openfortivpn"

# 4. SIGTERM (NetworkManager stopping the service) doesn't hang the service.
start_connected
stop_monitor
sudo -n kill -TERM "$svc_pid"
wait_gone 3 "$fake" && pass "SIGTERM stops openfortivpn" \
                    || fail "openfortivpn still running after SIGTERM"
wait_gone 3 "$svc_pid" && pass "service exits after SIGTERM" \
                       || fail "service hung after SIGTERM"

# 5. The gateway ends the session: the service reports reconnecting, starts
#    openfortivpn again with the same password and publishes the new tunnel.
start_connected FAKE_TUNDEV=ppp7
drop_tunnel
if wait_until 10 tunnels_reach 2; then
  pass "lost tunnel is restored"
else
  fail "lost tunnel not restored; openfortivpn started $(starts) times"
fi
sleep 0.3
# libnm reports started as soon as the second Config is out, before its
# Ip4Config; NetworkManager waits for both anyway.
reconnect=$(events | awk 'seen; $0 == "StateChanged 3" && ++n == 2 { seen = 1 }' | sort | paste -sd,)
if [ "$(states)" = "3 4 3 4" ] && [ "$reconnect" = "Config,Ip4Config,StateChanged 4" ]; then
  pass "service reports starting while reconnecting, then the new config"
else
  fail "unexpected signals: $(events | paste -sd,)"
fi
if [ "$(sed -n 's/^password //p' "$tmpdir/fake.log" | sort -u)" = secret ] &&
   [ "$(grep -c '^password ' "$tmpdir/fake.log")" = 2 ]; then
  pass "reconnect reuses the session password"
else
  fail "reconnect passwords: $(sed -n 's/^password //p' "$tmpdir/fake.log" | tr '\n' ' ')"
fi
alive "$svc_pid" && pass "service keeps running through the reconnect" \
                 || fail "service exited on tunnel loss"

# A tunnel that drops again right after coming back waits longer (2 s, not 1).
drop_tunnel
if wait_until 10 tunnels_reach 3; then
  wait=$(awk '$1 == "dropped" { d = $2 } $1 == "time" && d { w = $3 - d } END { printf "%.1f", w }' "$tmpdir/fake.log")
  awk -v w="$wait" 'BEGIN { exit !(w >= 1.9) }' \
    && pass "tunnel dropping right after a reconnect backs off (${wait} s)" \
    || fail "tunnel dropping right after a reconnect retried after ${wait} s"
else
  fail "tunnel not restored after dropping again"
fi
plugin_call Disconnect
wait_gone 3 "$svc_pid" || fail "service still running after disconnect"
stop_monitor

# 6. Attempts that can't reach the gateway are retried, further apart each
#    time (1, 2, 4 s in the test build).
start_connected
set_mode unreachable
drop_tunnel
if wait_until 12 starts_reach 4; then
  gaps=$(start_gaps | tail -n2 | paste -sd' ')
  if awk -v g="$gaps" 'BEGIN { split(g, a, " "); exit !(a[1] >= 1 && a[2] >= 2 * a[1] - 0.5) }'; then
    pass "failed attempts are retried with a growing delay ($gaps s)"
  else
    fail "retry delays don't grow: $gaps s"
  fi
else
  fail "failed attempts not retried; openfortivpn started $(starts) times"
fi
set_mode up
if wait_until 15 tunnels_reach 2; then
  sleep 0.3
  [ "$(states)" = "3 4 3 4" ] \
    && pass "tunnel restored once the gateway is reachable" \
    || fail "tunnel up again but states went $(states)"
else
  fail "tunnel not restored after the gateway came back"
fi
plugin_call Disconnect
wait_gone 3 "$svc_pid" || fail "service still running after disconnect"
stop_monitor

# 7. Disconnecting while waiting to reconnect ends the session for good.
start_connected
set_mode unreachable
drop_tunnel
wait_until 5 starts_reach 2
plugin_call Disconnect
wait_gone 3 "$svc_pid" && pass "disconnect while reconnecting stops the service" \
                       || fail "service still running after disconnect while reconnecting"
n=$(starts)
sleep 3
[ "$(starts)" = "$n" ] && pass "no attempts after disconnect" \
                       || fail "openfortivpn started after disconnect"
stop_monitor

# 8. A rejected login while reconnecting stops retrying and reports why.
start_connected
set_mode reject
drop_tunnel
if wait_gone 5 "$svc_pid"; then
  pass "rejected login while reconnecting stops the service"
else
  fail "service still running after a rejected login"
fi
[ "$(starts)" = 2 ] && pass "no retries after a rejected login" \
                    || fail "openfortivpn started $(starts) times after a rejected login"
stop_monitor
[ "$(last_failure)" = "Failure 0" ] && pass "rejected login is reported as login failed" \
                                    || fail "rejected login reported as $(last_failure)"
[ "$(events | tail -n1)" = "StateChanged 6" ] || fail "last signal is $(events | tail -n1), not stopped"

# 9. A rejected login on the first connect fails it without retrying.
initial_mode=reject start_connected
wait_gone 5 "$svc_pid" && pass "rejected first login stops the service" \
                       || fail "service still running after a rejected first login"
[ "$(starts)" = 1 ] || fail "openfortivpn started $(starts) times after a rejected first login"
stop_monitor
[ "$(last_failure)" = "Failure 0" ] && pass "rejected first login is reported as login failed" \
                                    || fail "rejected first login reported as $(last_failure)"

# 10. While the gateway's name doesn't resolve, which NetworkManager causes
#     by keeping DNS off until the VPN is back, reconnect to the address it
#     last resolved to and name the gateway in the TLS handshake.
name=vpn.nm-openfortivpn.test
echo "192.0.2.1 $name" > "$tmpdir/hosts"
hosts=$tmpdir/hosts gateway=$name start_connected
: > "$tmpdir/hosts"
drop_tunnel
if wait_until 10 tunnels_reach 2; then
  pass "tunnel restored while the gateway's name doesn't resolve"
else
  fail "tunnel not restored without DNS; openfortivpn started $(starts) times"
fi
args=$(sed -n 's/^args //p' "$tmpdir/fake.log")
first=$(head -n1 <<< "$args" | cut -d' ' -f1)
retry=$(tail -n1 <<< "$args")
[ "$first" = "$name" ] && pass "first connect uses the gateway's name" \
                       || fail "first connect went to $first"
if [ "${retry%% *}" = 192.0.2.1 ] && [[ " $retry " = *" --sni=$name "* ]]; then
  pass "reconnect uses the last address with the name as SNI"
else
  fail "reconnect args: $retry"
fi
sleep 0.3
stop_monitor
# Both Configs carry 192.0.2.1, as a uint32 in network byte order read on a
# little-endian host. libnm copies it into the Ip4Configs too, so skip those.
configs=$(grep 'VPN\.Plugin\.Config ' "$tmpdir/monitor.log")
if [ "$(grep -c . <<< "$configs")" = 2 ] &&
   [ "$(grep -c "'gateway': <uint32 16908480>" <<< "$configs")" = 2 ]; then
  pass "reconnected config keeps the gateway address"
else
  fail "gateway address in configs: $(grep -o "'gateway': <[^>]*>" <<< "$configs" | paste -sd' ')"
fi
plugin_call Disconnect
wait_gone 3 "$svc_pid" || fail "service still running after disconnect"

# 11. A gateway that stays out of reach is given up on after
#     RECONNECT_ATTEMPTS_MAX (5 in the test build) attempts, so NetworkManager
#     takes the VPN down and switches the other network's DNS back on.
start_connected
set_mode unreachable
drop_tunnel
if wait_gone 45 "$svc_pid"; then
  pass "service gives up on a gateway that stays out of reach"
else
  fail "service still reconnecting; openfortivpn started $(starts) times"
fi
[ "$(starts)" = 6 ] && pass "gave up after 5 attempts" \
                    || fail "openfortivpn started $(starts) times, not 6"
stop_monitor
[ "$(last_failure)" = "Failure 1" ] && pass "giving up is reported as connect failed" \
                                    || fail "giving up reported as $(last_failure)"
[ "$(events | tail -n1)" = "StateChanged 6" ] || fail "last signal is $(events | tail -n1), not stopped"

exit $(( failures > 0 ))
