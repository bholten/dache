#!/usr/bin/env bash
# Atomic-write contract: the destination file must never exist in a
# half-written state — it appears atomically via rename, or not at all.
#
# We assert this two ways:
#   1. Smoke test: parallel writers don't corrupt the final archive.
#   2. Kill-mid-write: send SIGKILL to a writer with a large input, then
#      check that the destination either doesn't exist OR is a valid
#      complete archive. Pre-fix, the file existed and was torn.

set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
. "$SCRIPT_DIR/lib.sh"

start_test "parallel build cache writes produce a valid archive"
new_workspace

mkdir src
head -c 1048576 /dev/urandom > src/input.bin

  # Shell redirection (always-overwrite) avoids `cp`'s EEXIST race when
  # multiple processes write to the same path concurrently; we want this
  # test to fail only on archive-write atomicity bugs, not on cp races.
CMD='mkdir -p build && cat src/input.bin > build/output.bin && sleep 0.5'

pids=()
for i in 1 2 3 4 5 6 7 8; do
  ("$DACHE_BIN" -i src/ -o build/ -- sh -c "$CMD" > /dev/null 2>&1) &
  pids+=("$!")
done

failures=0
for pid in "${pids[@]}"; do
  if ! wait "$pid"; then failures=$((failures + 1)); fi
done
assert_eq "0" "$failures" "all parallel invocations should exit 0"

cache_file="$(find "$HOME/.cache/dache" -name '*.tar.gz' 2>/dev/null | head -1)"
assert_file_exists "$cache_file"

# Verify the archive is structurally valid: gzip first, then tar.
gunzip -t "$cache_file"
assert_eq "0" "$?" "archive is a valid gzip stream"

tar -tzf "$cache_file" > /dev/null 2>&1
assert_eq "0" "$?" "archive is a valid tar"

# Restored content matches the original input.
rm -rf build
"$DACHE_BIN" -i src/ -o build/ -- sh -c "$CMD" > /dev/null 2>&1
sha_in="$(sha256sum src/input.bin | awk '{print $1}')"
sha_out="$(sha256sum build/output.bin | awk '{print $1}')"
assert_eq "$sha_in" "$sha_out" "restored output matches original input"

cleanup_workspace
print_summary

start_test "SIGKILL during write leaves dest absent, not torn"
new_workspace

mkdir src build
# 32MB so write_archive spends real time gzip-encoding.
head -c 33554432 /dev/urandom > src/input.bin

CMD='cp src/input.bin build/output.bin'

# Start a write in the background and kill it shortly after the command
# completes (i.e. during write_archive).
"$DACHE_BIN" -i src/ -o build/ -- sh -c "$CMD" > /dev/null 2>&1 &
dache_pid=$!

# Wait until the command finishes (build/output.bin appears), then kill
# while write_archive is running.
for _ in $(seq 1 100); do
  if [ -e build/output.bin ]; then break; fi
  sleep 0.01
done
# Give write_archive a tiny moment to start writing the tmp file.
sleep 0.02
kill -9 "$dache_pid" 2>/dev/null || true
wait "$dache_pid" 2>/dev/null || true

cache_dir="$HOME/.cache/dache"
final_count="$(find "$cache_dir" -maxdepth 1 -name '*.tar.gz' 2>/dev/null | wc -l)"

if [ "$final_count" = "0" ]; then
  # Killed before rename: dest never existed. Atomic behavior, correct.
  TESTS_PASSED=$((TESTS_PASSED + 1))
elif [ "$final_count" = "1" ]; then
  # Killed after rename: dest is a fully-written archive. Atomic, also correct.
  cache_file="$(find "$cache_dir" -maxdepth 1 -name '*.tar.gz' | head -1)"
  if gunzip -t "$cache_file" 2>/dev/null && tar -tzf "$cache_file" > /dev/null 2>&1; then
    TESTS_PASSED=$((TESTS_PASSED + 1))
  else
    TESTS_FAILED=$((TESTS_FAILED + 1))
    echo "  FAIL [$CURRENT_TEST]: dest file exists but is torn: $cache_file" >&2
  fi
else
  TESTS_FAILED=$((TESTS_FAILED + 1))
  echo "  FAIL [$CURRENT_TEST]: unexpected $final_count cache files" >&2
fi

# A leftover .tmp.<pid> is acceptable (the killed writer couldn't clean up),
# but the user can mop it up with a single rm. The important invariant is
# that the *destination* path is never in an intermediate state.

cleanup_workspace
print_summary

start_test "parallel snapshots leave a valid manifest"
new_workspace

mkdir assets
head -c 65536 /dev/urandom > assets/asset.bin

pids=()
for i in 1 2 3 4 5 6; do
  ("$DACHE_BIN" snapshot -o "manifest.json" assets/ > /dev/null 2>&1) &
  pids+=("$!")
done

failures=0
for pid in "${pids[@]}"; do
  if ! wait "$pid"; then failures=$((failures + 1)); fi
done
assert_eq "0" "$failures" "all parallel snapshots should exit 0"

assert_file_exists "manifest.json"

leftover_count="$(find . -maxdepth 1 -name 'manifest.json.tmp.*' 2>/dev/null | wc -l)"
assert_eq "0" "$leftover_count" "no leftover manifest .tmp.<pid> files"

if command -v python3 >/dev/null 2>&1; then
  python3 -c "import json,sys; json.load(open('manifest.json'))" 2>/dev/null
  assert_eq "0" "$?" "manifest is valid JSON"
fi

rm -rf assets
"$DACHE_BIN" restore manifest.json > /dev/null 2>&1
assert_file_exists "assets/asset.bin"

cleanup_workspace
print_summary
