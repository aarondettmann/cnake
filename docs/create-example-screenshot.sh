#!/usr/bin/env bash
set -euo pipefail

# Requires termframe: https://github.com/pamburus/termframe

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source_dir="$(cd -- "$script_dir/.." && pwd)"
output_dir="${1:-"$script_dir"}"

mkdir -p -- "$output_dir"

tmpdir="$(mktemp -d "${TMPDIR:-/tmp}/cnake-screenshot.XXXXXX")"
cleanup() {
  rm -rf -- "$tmpdir"
}
trap cleanup EXIT

cp -- "$source_dir/Makefile" "$source_dir/cnake.c" "$tmpdir/"
make --no-print-directory -C "$tmpdir"

output="$output_dir/cnake-screenshot.svg"
seed=1729

termframe \
  --title "cnake" \
  --mode dark \
  --width 42 \
  --height 14 \
  --window-margin 0 \
  --timeout 1 \
  --output "$output" \
  "$tmpdir/cnake" 6 --seed "$seed"

printf 'Wrote %s\n' "$output"
