#!/bin/sh
set -eu

# The auth dialog only returns secrets. Saving them is the secret agent's job:
# a keyring write or connection update from here makes the GNOME agent delete
# the saved password (see README "Passwords").

readelf_bin=$1
auth_dialog=$2

syms=$("$readelf_bin" --dyn-syms -W "$auth_dialog")

for forbidden in secret_password_store secret_password_clear \
                 nm_remote_connection_update2 nm_remote_connection_commit_changes; do
  if printf '%s\n' "$syms" | grep -q "UND ${forbidden}"; then
    echo "$auth_dialog must not call $forbidden" >&2
    exit 1
  fi
done
