#!/usr/bin/env bash
# Format-version checks: dache must refuse to extract archives that lack
# its format marker. The integration question this answers is whether the
# CLI degrades gracefully (cache miss → re-run command → overwrite the bad
# archive) rather than leaking foreign content into cwd.

set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
. "$SCRIPT_DIR/lib.sh"

start_test "foreign archive in cache is rejected, command re-runs cleanly"
new_workspace

mkdir src
echo "input" > src/in.txt

# Prime the cache. The second run must use the SAME command (and inputs,
# env) so it computes the same key and looks at the cache slot we then
# corrupt below.
CMD='mkdir -p build && cat src/in.txt > build/out.txt'
"$DACHE_BIN" -i src/in.txt -o build/ -- sh -c "$CMD" > /dev/null 2>&1
cache_file="$(find "$HOME/.cache/dache" -maxdepth 1 -name '*.tar.gz' | head -1)"
assert_file_exists "$cache_file"

# Replace it with a tar.gz built by system `tar` (no dache marker).
mkdir foreign
echo "evil" > foreign/evil.txt
tar -czf "$cache_file" -C foreign evil.txt
rm -rf foreign build

# Re-running the same command should: try the corrupted archive, log the
# marker rejection, fall through to running the command, and re-cache.
output="$("$DACHE_BIN" -i src/in.txt -o build/ -- sh -c "$CMD" 2>&1)"

assert_contains "$output" "missing format marker"
assert_file_exists "build/out.txt"
assert_file_content "build/out.txt" "input"

# Critical: foreign content must not have been extracted into cwd.
if [ -e "evil.txt" ]; then
  TESTS_FAILED=$((TESTS_FAILED + 1))
  echo "  FAIL [$CURRENT_TEST]: foreign evil.txt was extracted into cwd" >&2
else
  TESTS_PASSED=$((TESTS_PASSED + 1))
fi

# The cache file should have been replaced with a valid dache archive.
new_cache_file="$(find "$HOME/.cache/dache" -maxdepth 1 -name '*.tar.gz' | head -1)"
assert_file_exists "$new_cache_file"
first_entry="$(tar -tzf "$new_cache_file" | head -1)"
assert_eq "__dache_format_v1" "$first_entry"

cleanup_workspace
print_summary

start_test "manifest with unknown version is rejected on restore"
new_workspace

mkdir assets
echo "asset payload" > assets/file.txt

"$DACHE_BIN" snapshot -o manifest.json assets/ > /dev/null 2>&1
assert_file_exists "manifest.json"

# Forge a version bump and try to restore.
sed -i 's/"version": 1/"version": 99/' manifest.json

rm -rf assets
output="$("$DACHE_BIN" restore manifest.json 2>&1 || true)"
assert_contains "$output" "version 99 is not supported"

# assets/file.txt must NOT have been restored from the rejected manifest.
if [ -e "assets/file.txt" ]; then
  TESTS_FAILED=$((TESTS_FAILED + 1))
  echo "  FAIL [$CURRENT_TEST]: restore ran despite version rejection" >&2
else
  TESTS_PASSED=$((TESTS_PASSED + 1))
fi

cleanup_workspace
print_summary
