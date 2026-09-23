#!/bin/sh
set -eu

# The auth dialog never writes the keyring itself. The secret agent owns it:
# its save deletes every item for the connection before writing, so a second
# writer's item is lost (see README "Passwords").

readelf_bin=$1
auth_dialog=$2

syms=$("$readelf_bin" --dyn-syms -W "$auth_dialog")

for forbidden in secret_password_store secret_password_clear secret_item_create; do
  if printf '%s\n' "$syms" | grep -q "UND ${forbidden}"; then
    echo "$auth_dialog must not call $forbidden" >&2
    exit 1
  fi
done
