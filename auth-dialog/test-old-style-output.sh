#!/bin/sh
set -eu

auth_dialog=$1
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

input='DATA_KEY=user
DATA_VAL=alice

SECRET_KEY=password
SECRET_VAL=stdin-secret

DONE
QUIT
'

expected_file=$tmpdir/expected
output_file=$tmpdir/output

printf '%s' 'password
stdin-secret


' > "$expected_file"

printf '%s' "$input" | "$auth_dialog" \
  -u test-uuid \
  -n test-vpn \
  -s org.freedesktop.NetworkManager.openfortivpn > "$output_file"

cmp "$expected_file" "$output_file"
