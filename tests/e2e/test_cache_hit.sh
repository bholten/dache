#!/usr/bin/env bash
# Build cache: first run misses + caches; second run hits + restores.

set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
. "$SCRIPT_DIR/lib.sh"

start_test "cache miss runs command, cache hit restores output"
new_workspace

mkdir src build
echo "alpha" > src/input.txt

# Use a sentinel side-effect outside the cached output set so we can tell
# whether the command actually ran (it writes the marker; the marker is not
# in -o so it isn't restored from the archive).
CMD='cp src/input.txt build/output.txt; touch ran.marker'

# First run: cache miss, command runs.
miss_out="$("$DACHE_BIN" -i src/ -o build/ -- sh -c "$CMD" 2>&1)"
assert_contains "$miss_out" "cache miss"
assert_contains "$miss_out" "cached (local)"
assert_file_content "build/output.txt" "alpha"
assert_file_exists "ran.marker"

# Wipe outputs + marker.
rm -rf build/output.txt ran.marker

# Second run: cache hit. Command must NOT run (no marker), output restored.
hit_out="$("$DACHE_BIN" -i src/ -o build/ -- sh -c "$CMD" 2>&1)"
assert_contains "$hit_out" "cache hit"
assert_not_contains "$hit_out" "cache miss"
assert_file_content "build/output.txt" "alpha"
[ ! -e ran.marker ]
assert_eq "0" "$?" "ran.marker should not exist on cache hit"

cleanup_workspace
print_summary
