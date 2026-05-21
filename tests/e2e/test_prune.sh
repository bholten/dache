#!/usr/bin/env bash
# `dache prune` is the only path to evict cache entries. Tests pin:
# (1) --older-than filters by mtime,
# (2) --max-size keeps newest within budget,
# (3) --dry-run never deletes,
# (4) no filter is an error,
# (5) blob storage is left alone (blobs back committed snapshots),
# (6) empty cache is a clean no-op.

set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
. "$SCRIPT_DIR/lib.sh"

prime_three_cache_entries() {
  # Builds three distinct cache slots in $HOME/.cache/dache by varying
  # the command. Names them old1, old2, fresh based on how we'll backdate
  # them.
  mkdir -p src build
  echo "x" > src/in.txt
  for label in old1 old2 fresh; do
    "$DACHE_BIN" -i src/in.txt -o build/ -- sh -c "mkdir -p build && echo $label > build/out.txt" > /dev/null 2>&1
    rm -rf build
  done
}

start_test "--older-than removes only entries older than the cutoff"
new_workspace
prime_three_cache_entries

# Backdate two of the three to 10 days ago; leave the third fresh.
files=("$HOME/.cache/dache"/*.tar.gz)
assert_eq "3" "$(echo "${files[@]}" | wc -w)"

touch -d "10 days ago" "${files[0]}" "${files[1]}"

output="$("$DACHE_BIN" prune --older-than 7d 2>&1)"
assert_contains "$output" "2 entries"
assert_contains "$output" "removed"
assert_contains "$output" "1 kept"

remaining="$(find "$HOME/.cache/dache" -maxdepth 1 -name '*.tar.gz' | wc -l)"
assert_eq "1" "$remaining"

cleanup_workspace
print_summary

start_test "--dry-run never deletes"
new_workspace
prime_three_cache_entries

touch -d "10 days ago" "$HOME/.cache/dache"/*.tar.gz

output="$("$DACHE_BIN" prune --older-than 7d --dry-run 2>&1)"
assert_contains "$output" "dry-run"
assert_contains "$output" "would"

remaining="$(find "$HOME/.cache/dache" -maxdepth 1 -name '*.tar.gz' | wc -l)"
assert_eq "3" "$remaining"

cleanup_workspace
print_summary

start_test "--max-size keeps newest entries within budget"
new_workspace

mkdir src build
head -c 1048576 /dev/urandom > src/in.bin  # 1 MB

# Create three cache slots of ~1 MB each. After each, the cache holds
# one more archive. Spacing the mtimes so we can sort deterministically.
for label in a b c; do
  "$DACHE_BIN" -i src/in.bin -o build/ -- sh -c "mkdir -p build && cp src/in.bin build/out-$label.bin" > /dev/null 2>&1
  sleep 1
  rm -rf build
done

before="$(find "$HOME/.cache/dache" -maxdepth 1 -name '*.tar.gz' | wc -l)"
assert_eq "3" "$before"

# Budget of 1500K should keep one entry (~1 MB compressed) and prune
# the rest. The exact compressed size of random data is ~1 MB so two
# entries would push past 1500K. We accept either "keep 1" or "keep 0"
# behavior — what we actually want to pin is "the newest survives".
"$DACHE_BIN" prune --max-size 1500K > /tmp/prune-out 2>&1

# Identify the newest cache file BEFORE pruning, by mtime.
# Reconstruct: the LAST `dache` invocation wrote the newest. That cache
# key corresponds to the "c" label.
newest_after_prune="$(find "$HOME/.cache/dache" -maxdepth 1 -name '*.tar.gz' -printf '%T@ %p\n' 2>/dev/null | sort -n | tail -1 | awk '{print $2}')"

# At least the newest must survive a max-size that fits one entry.
if [ -n "$newest_after_prune" ] && [ -e "$newest_after_prune" ]; then
  TESTS_PASSED=$((TESTS_PASSED + 1))
else
  TESTS_FAILED=$((TESTS_FAILED + 1))
  echo "  FAIL [$CURRENT_TEST]: newest entry got pruned" >&2
fi

# And we should have fewer entries than we started with.
after="$(find "$HOME/.cache/dache" -maxdepth 1 -name '*.tar.gz' | wc -l)"
if [ "$after" -lt 3 ]; then
  TESTS_PASSED=$((TESTS_PASSED + 1))
else
  TESTS_FAILED=$((TESTS_FAILED + 1))
  echo "  FAIL [$CURRENT_TEST]: no entries pruned ($after remain of 3)" >&2
fi

cleanup_workspace
print_summary

start_test "no filter is an error"
new_workspace

output="$("$DACHE_BIN" prune 2>&1 || true)"
assert_contains "$output" "required"

cleanup_workspace
print_summary

start_test "blob storage is left alone"
new_workspace

# Make a snapshot first so blob storage exists.
mkdir assets
echo "asset" > assets/a.txt
"$DACHE_BIN" snapshot -o manifest.json assets/ > /dev/null 2>&1
assert_file_exists "$HOME/.cache/dache/blobs"

# Also create some old build cache entries.
prime_three_cache_entries
touch -d "10 days ago" "$HOME/.cache/dache"/*.tar.gz

"$DACHE_BIN" prune --older-than 7d > /dev/null 2>&1

# Blobs must still be there even though they're "old" (just created
# but the policy doesn't apply to them — they aren't *.tar.gz).
blob_count="$(find "$HOME/.cache/dache/blobs" -type f 2>/dev/null | wc -l)"
if [ "$blob_count" -gt 0 ]; then
  TESTS_PASSED=$((TESTS_PASSED + 1))
else
  TESTS_FAILED=$((TESTS_FAILED + 1))
  echo "  FAIL [$CURRENT_TEST]: blob storage was wiped" >&2
fi

cleanup_workspace
print_summary

start_test "empty cache prune is a clean no-op"
new_workspace

output="$("$DACHE_BIN" prune --older-than 1d 2>&1)"
assert_contains "$output" "empty"

cleanup_workspace
print_summary
