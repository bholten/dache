#!/usr/bin/env bash
# Snapshot/restore roundtrip: hash + store + manifest + restore.

set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
. "$SCRIPT_DIR/lib.sh"

start_test "snapshot stores files, restore puts them back identically"
new_workspace

mkdir -p assets/textures assets/models
echo "texture data" > assets/textures/foo.png
echo "model data"   > assets/models/bar.obj
chmod 0755 assets/models/bar.obj

# Snapshot.
"$DACHE_BIN" snapshot -o assets.json assets/ > /dev/null 2>&1
assert_file_exists "assets.json"

# Capture original sums for comparison.
orig_foo="$(sha256sum assets/textures/foo.png | awk '{print $1}')"
orig_bar="$(sha256sum assets/models/bar.obj  | awk '{print $1}')"

# Wipe assets.
rm -rf assets

# Restore.
"$DACHE_BIN" restore assets.json > /dev/null 2>&1
assert_file_exists "assets/textures/foo.png"
assert_file_exists "assets/models/bar.obj"

new_foo="$(sha256sum assets/textures/foo.png | awk '{print $1}')"
new_bar="$(sha256sum assets/models/bar.obj  | awk '{print $1}')"
assert_eq "$orig_foo" "$new_foo" "foo.png hash mismatch"
assert_eq "$orig_bar" "$new_bar" "bar.obj hash mismatch"

# Mode is preserved.
mode_bar="$(stat -c '%a' assets/models/bar.obj 2>/dev/null || stat -f '%Lp' assets/models/bar.obj)"
assert_eq "755" "$mode_bar" "bar.obj mode not preserved"

cleanup_workspace
print_summary
