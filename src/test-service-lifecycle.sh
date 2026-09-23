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
alive() { [ -d "/proc/$1" ]; }

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

# start_connected [ENV=VALUE...] -- start the service and connect it
start_connected() {
  # Remember every earlier service and fake so cleanup can kill them all.
  echo $(fake_pids) $svc_pid >> "$tmpdir/all-pids"
  : > "$tmpdir/fake.log"
  : > "$tmpdir/monitor.log"
  bus=org.freedesktop.NetworkManager.openfortivpn.Test$$_$RANDOM
  gdbus monitor --system --dest "$bus" > "$tmpdir/monitor.log" 2>&1 &
  monitor_pid=$!
  sudo -n env FAKE_LOG="$tmpdir/fake.log" "$@" "$service" --bus-name "$bus" \
    > "$tmpdir/service.log" 2>&1 &
  for _ in $(seq 50); do
    svc_pid=$(pgrep -f "^$service --bus-name $bus\$")
    [ -n "$svc_pid" ] && gdbus introspect --system --dest "$bus" \
      --object-path /org/freedesktop/NetworkManager/VPN/Plugin >/dev/null 2>&1 && break
    sleep 0.1
  done
  plugin_call Connect "{'connection': {'id': <'test'>, 'uuid': <'00000000-0000-0000-0000-000000000001'>, 'type': <'vpn'>},
    'vpn': {'service-type': <'org.freedesktop.NetworkManager.openfortivpn'>,
            'data': <{'gateway': '192.0.2.1', 'user': 'test'}>,
            'secrets': <{'password': 'secret'}>}}"
  for _ in $(seq 50); do
    grep -q 'Tunnel is up' "$tmpdir/service.log" && break
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

exit $(( failures > 0 ))
