#!/usr/bin/env bash
set -euo pipefail

# Requires termframe: https://github.com/pamburus/termframe

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source_dir="$(cd -- "$script_dir/.." && pwd)"
output_dir="${1:-"$script_dir"}"

mkdir -p -- "$output_dir" "$source_dir/build"

workdir="$(mktemp -d "$source_dir/build/create-example-screenshot.XXXXXX")"
cleanup() {
  rm -rf -- "$workdir"
}
trap cleanup EXIT

cp -- "$source_dir/Makefile" "$source_dir/VERSION" "$workdir/"
cp -R -- "$source_dir/src" "$workdir/"
make --no-print-directory -C "$workdir"

seed=1729

create_screenshot() {
  local output="$1"
  shift

  termframe \
    --title "cnake" \
    --mode dark \
    --width 42 \
    --height 14 \
    --window-margin 0 \
    --timeout 1 \
    --output "$output" \
    -- \
    "$workdir/cnake" "$@" 6 --seed "$seed"

  printf 'Wrote %s\n' "$output"
}

create_screenshot "$output_dir/cnake-screenshot.svg"
create_screenshot "$output_dir/cnake-ascii-screenshot.svg" --ascii
