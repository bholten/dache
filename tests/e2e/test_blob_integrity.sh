#!/usr/bin/env bash
# Blob integrity: a corrupted remote blob must be detected on restore.

set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
. "$SCRIPT_DIR/lib.sh"

start_test "restore rejects a tampered remote blob"
new_workspace

remote="$WORKDIR/remote-blobs"
mkdir -p "$remote"

# Snapshot a file as machine A.
home_a="$WORKDIR/home_a"
mkdir -p "$home_a"
machine_a="$WORKDIR/a"
mkdir -p "$machine_a/assets"
echo "original" > "$machine_a/assets/payload.txt"
(cd "$machine_a" && HOME="$home_a" "$DACHE_BIN" snapshot -o assets.json -r "$remote" assets/ > /dev/null 2>&1)

# Tamper with the blob in the remote (and remove machine A's local copy so
# restore is forced to use the remote).
blob_file="$(ls "$remote/blobs/")"
echo "TAMPERED" > "$remote/blobs/$blob_file"

# Machine B restores; must detect mismatch and fail.
home_b="$WORKDIR/home_b"
mkdir -p "$home_b"
machine_b="$WORKDIR/b"
mkdir -p "$machine_b"
cp "$machine_a/assets.json" "$machine_b/assets.json"

out="$(cd "$machine_b" && HOME="$home_b" "$DACHE_BIN" restore -r "$remote" assets.json 2>&1 || true)"
assert_contains "$out" "integrity mismatch"
# The tampered content must NOT land in the restore target.
if [ -e "$machine_b/assets/payload.txt" ]; then
  content="$(cat "$machine_b/assets/payload.txt")"
  assert_eq "" "$content" "tampered content was written"
fi

cleanup_workspace
print_summary
