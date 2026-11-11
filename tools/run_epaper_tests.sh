#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$(mktemp -d)"
trap 'rm -rf "$BUILD"' EXIT
"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror ${EPAPER_TEST_CFLAGS:-} \
  -I"$ROOT/test/epaper_stubs" -I"$ROOT/components/seeed_epaper" \
  "$ROOT/test/epaper_panel_test.c" "$ROOT/components/seeed_epaper/epaper_panel.c" \
  "$ROOT"/components/seeed_epaper/driver/*.c -o "$BUILD/epaper_panel_test"
"$BUILD/epaper_panel_test"
