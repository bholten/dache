#!/usr/bin/env bash
# Remote dir round trip: machine A caches, machine B (separate HOME)
# fetches the cached output from the shared remote dir.

set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
. "$SCRIPT_DIR/lib.sh"

start_test "build cache is shared via --remote"
new_workspace

remote="$WORKDIR/remote-cache"
mkdir -p "$remote"
CMD='cp src/input.txt build/output.txt; touch ran.marker'

# Machine A: build with remote.
home_a="$WORKDIR/home_a"
mkdir -p "$home_a"
machine_a="$WORKDIR/a"
mkdir -p "$machine_a/src" "$machine_a/build"
echo "shared" > "$machine_a/src/input.txt"

a_out="$(cd "$machine_a" && HOME="$home_a" "$DACHE_BIN" -i src/ -o build/ -r "$remote" -- sh -c "$CMD" 2>&1)"
assert_contains "$a_out" "cache miss"
assert_contains "$a_out" "pushed to remote"

# Machine B: same command and inputs but separate HOME (empty local cache).
# Must pull from remote and skip the command.
home_b="$WORKDIR/home_b"
mkdir -p "$home_b"
machine_b="$WORKDIR/b"
mkdir -p "$machine_b/src" "$machine_b/build"
echo "shared" > "$machine_b/src/input.txt"

b_out="$(cd "$machine_b" && HOME="$home_b" "$DACHE_BIN" -i src/ -o build/ -r "$remote" -- sh -c "$CMD" 2>&1)"
assert_contains "$b_out" "cache hit (remote)"
assert_file_content "$machine_b/build/output.txt" "shared"
[ ! -e "$machine_b/ran.marker" ]
assert_eq "0" "$?" "command must not run on remote cache hit"

cleanup_workspace
print_summary

# Second test scope.
start_test "blob snapshot is shared via --remote"
new_workspace

remote="$WORKDIR/remote-blobs"
mkdir -p "$remote"

# Machine A snapshots.
home_a="$WORKDIR/home_a"
mkdir -p "$home_a"
machine_a="$WORKDIR/a"
mkdir -p "$machine_a/assets"
echo "asset-bytes" > "$machine_a/assets/file.bin"

(cd "$machine_a" && HOME="$home_a" "$DACHE_BIN" snapshot -o assets.json -r "$remote" assets/ > /dev/null 2>&1)

# Machine B restores from the remote (without machine A's local cache).
home_b="$WORKDIR/home_b"
mkdir -p "$home_b"
machine_b="$WORKDIR/b"
mkdir -p "$machine_b"
cp "$machine_a/assets.json" "$machine_b/assets.json"

(cd "$machine_b" && HOME="$home_b" "$DACHE_BIN" restore -r "$remote" assets.json > /dev/null 2>&1)
assert_file_content "$machine_b/assets/file.bin" "asset-bytes"

cleanup_workspace
print_summary
