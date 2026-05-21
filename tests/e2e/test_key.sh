#!/usr/bin/env bash
# `dache key` is the diagnostic sibling of `dache cache` — it prints the
# cache key plus the framed components without running the command. This
# test pins that contract: the printed key must equal the path `dache
# cache` actually caches at, and the same inputs/env/command components
# that change the key for `dache cache` must change it here too.

set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
. "$SCRIPT_DIR/lib.sh"

extract_key() {
  # Pulls "key: <hex>" out of the first line of `dache key` output.
  awk '/^key: /{print $2; exit}'
}

start_test "dache key prints the same key dache cache uses"
new_workspace

mkdir src
echo "hello" > src/in.txt

# Use IDENTICAL inputs/env/command across both invocations — if dache key
# diverges from dache cache, that's the contract this test pins down.
CMD='sh -c "mkdir -p build && echo out > build/out.txt"'
key_output="$(eval "\"$DACHE_BIN\" key -i src/in.txt -e CC=gcc -- $CMD")"
printed_key="$(echo "$key_output" | extract_key)"

if echo "$printed_key" | grep -qE '^[0-9a-f]{64}$'; then
  TESTS_PASSED=$((TESTS_PASSED + 1))
else
  TESTS_FAILED=$((TESTS_FAILED + 1))
  echo "  FAIL [$CURRENT_TEST]: bad key format: '$printed_key'" >&2
fi

eval "\"$DACHE_BIN\" -i src/in.txt -e CC=gcc -o build/ -- $CMD" > /dev/null 2>&1
cache_file="$(find "$HOME/.cache/dache" -maxdepth 1 -name '*.tar.gz' | head -1)"
cache_key_from_file="$(basename "$cache_file" .tar.gz)"
assert_eq "$printed_key" "$cache_key_from_file"

cleanup_workspace
print_summary

start_test "dache key shows env, inputs, and command sections"
new_workspace

mkdir src
echo "x" > src/a.txt
echo "y" > src/b.txt

output="$("$DACHE_BIN" key -i src/ -e CFLAGS=-O2 -- make a b)"

assert_contains "$output" "^key: "
assert_contains "$output" "env (1):"
assert_contains "$output" "CFLAGS=-O2"
assert_contains "$output" "inputs (2):"
assert_contains "$output" "src/a.txt"
assert_contains "$output" "src/b.txt"
assert_contains "$output" "command (3):"

cleanup_workspace
print_summary

start_test "changing input content changes the printed key"
new_workspace

mkdir src
echo "v1" > src/in.txt

key1="$("$DACHE_BIN" key -i src/in.txt -- make | extract_key)"

echo "v2" > src/in.txt
key2="$("$DACHE_BIN" key -i src/in.txt -- make | extract_key)"

if [ "$key1" != "$key2" ]; then
  TESTS_PASSED=$((TESTS_PASSED + 1))
else
  TESTS_FAILED=$((TESTS_FAILED + 1))
  echo "  FAIL [$CURRENT_TEST]: key didn't change when input did ($key1)" >&2
fi

cleanup_workspace
print_summary

start_test "-o and -r do not affect the printed key"
new_workspace

mkdir src
echo "z" > src/in.txt

key_no_extras="$("$DACHE_BIN" key -i src/in.txt -- make | extract_key)"
key_with_extras="$("$DACHE_BIN" key -i src/in.txt -o build/ -r /tmp/some-remote -- make | extract_key)"

assert_eq "$key_no_extras" "$key_with_extras"

cleanup_workspace
print_summary

start_test "env order does not affect the printed key"
new_workspace

mkdir src
echo "z" > src/in.txt

key_ab="$("$DACHE_BIN" key -i src/in.txt -e A=1 -e B=2 -- make | extract_key)"
key_ba="$("$DACHE_BIN" key -i src/in.txt -e B=2 -e A=1 -- make | extract_key)"

assert_eq "$key_ab" "$key_ba"

cleanup_workspace
print_summary
