#!/usr/bin/env bash
# Runs every host/scripts/*.txt against the host harness and reports pass or fail.
# Run from firmware/:
#
#   host/run-tests.sh              build, then run all scripts
#   host/run-tests.sh --no-build   run against the existing build
#   host/run-tests.sh --update     rewrite the golden files (after an intended UI change)
#
# Then the game cores' own unit tests (host/packit, host/helgrind), which need no harness.
#
# A script may have a <name>.args file of extra harness options (--fs-dir ...).
# A script's exit status is the harness's; see wiki/systems/host-test-harness.md.

set -u
cd "$(dirname "$0")/.."

build=1
for arg in "$@"; do
  case "$arg" in
    --no-build) build=0 ;;
    --update) export HOST_UPDATE_GOLDENS=1 ;;
    *) echo "usage: host/run-tests.sh [--no-build] [--update]" >&2; exit 1 ;;
  esac
done

if [ "$build" = 1 ]; then
  pio run -e host || exit 1
fi

bin=.pio/build/host/program
out=.pio/host-out
mkdir -p "$out"
fail=0
for script in host/scripts/*.txt; do
  name=$(basename "$script" .txt)
  # Extra harness options for a script live beside it, in <name>.args.
  extra=""
  [ -f "host/scripts/$name.args" ] && extra=$(cat "host/scripts/$name.args")
  # A script that needs the window is skipped where there is no display.
  case "$extra" in
    *--window*) if [ -z "${DISPLAY:-}" ]; then echo "SKIP  $name (no DISPLAY)"; continue; fi ;;
  esac
  # shellcheck disable=SC2086
  if "$bin" --script "$script" --out "$out/$name" $extra >"$out/$name.log" 2>&1; then
    echo "PASS  $name"
  else
    rc=$?
    echo "FAIL  $name (exit $rc)  log: $out/$name.log"
    tail -n 5 "$out/$name.log" | sed 's/^/      /'
    fail=1
  fi
done

# Unit tests for the game cores. They build against the U8g2 checkout PlatformIO fetched.
unit() {
  name=$1; dir=$2; shift 2
  if make -C "$dir" "$@" >"$out/$name.log" 2>&1; then
    echo "PASS  $name"
  else
    echo "FAIL  $name  log: $out/$name.log"
    tail -n 5 "$out/$name.log" | sed 's/^/      /'
    fail=1
  fi
}
unit packit-core host/packit test
unit helgrind-core host/helgrind test check
exit $fail
