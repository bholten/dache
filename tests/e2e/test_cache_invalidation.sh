#!/usr/bin/env bash
# Cache key invalidation: changes to input, env, or command should miss.

set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
. "$SCRIPT_DIR/lib.sh"

start_test "input content change invalidates cache"
new_workspace
mkdir src build
echo "v1" > src/input.txt
"$DACHE_BIN" -i src/ -o build/ -- sh -c 'cp src/input.txt build/out.txt' > /dev/null 2>&1
rm -rf build/out.txt

echo "v2" > src/input.txt
out="$("$DACHE_BIN" -i src/ -o build/ -- sh -c 'cp src/input.txt build/out.txt' 2>&1)"
assert_contains "$out" "cache miss"
assert_file_content "build/out.txt" "v2"
cleanup_workspace

start_test "env var change invalidates cache"
new_workspace
mkdir src build
echo "x" > src/input.txt
"$DACHE_BIN" -i src/ -o build/ -e CC=gcc -- sh -c 'echo first > build/out.txt' > /dev/null 2>&1
rm -rf build/out.txt

out="$("$DACHE_BIN" -i src/ -o build/ -e CC=clang -- sh -c 'echo second > build/out.txt' 2>&1)"
assert_contains "$out" "cache miss"
assert_file_content "build/out.txt" "second"
cleanup_workspace

start_test "env var argv order does NOT invalidate cache"
new_workspace
mkdir src build
echo "x" > src/input.txt
CMD='echo cached_payload > build/out.txt; touch ran.marker'

"$DACHE_BIN" -i src/ -o build/ -e A=1 -e B=2 -- sh -c "$CMD" > /dev/null 2>&1
rm -rf build/out.txt ran.marker

# Swap argv order; cache key should match.
out="$("$DACHE_BIN" -i src/ -o build/ -e B=2 -e A=1 -- sh -c "$CMD" 2>&1)"
assert_contains "$out" "cache hit"
assert_file_content "build/out.txt" "cached_payload"
[ ! -e ran.marker ]
assert_eq "0" "$?" "command must not run on cache hit"
cleanup_workspace

start_test "command change invalidates cache"
new_workspace
mkdir src build
echo "x" > src/input.txt
"$DACHE_BIN" -i src/ -o build/ -- sh -c 'echo first > build/out.txt' > /dev/null 2>&1
rm -rf build/out.txt

out="$("$DACHE_BIN" -i src/ -o build/ -- sh -c 'echo second > build/out.txt' 2>&1)"
assert_contains "$out" "cache miss"
assert_file_content "build/out.txt" "second"
cleanup_workspace

print_summary
