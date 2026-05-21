#!/bin/sh
set -eu

readelf_bin=$1
plugin=$2

needed=$("$readelf_bin" -d "$plugin")

case "$needed" in
  *'Shared library: [libgtk-'* | *'Shared library: [libadwaita-'* | *'Shared library: [libnma-'*)
    echo "$plugin must not link GUI libraries from the libnm plugin entry point" >&2
    echo "$needed" >&2
    exit 1
    ;;
esac
