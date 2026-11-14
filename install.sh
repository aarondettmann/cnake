#!/usr/bin/env bash
# install-repos: tools
#
# Build cnake and install it with its Bash completion into ~/.local, or
# into $PREFIX. Safe to run unattended and again to update; the first
# comment marks it as such.
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

for tool in make cc; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "install.sh: '$tool' not found; install make and a C compiler" >&2
    exit 1
  }
done

make install PREFIX="${PREFIX:-$HOME/.local}"
