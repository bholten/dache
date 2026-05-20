# Common helpers for e2e tests. Sourced by tests/e2e/run.sh and test_*.sh.

set -u

# Resolve to the dache binary built at repo root.
DACHE_BIN="${DACHE_BIN:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/build/dache}"

if [ ! -x "$DACHE_BIN" ]; then
  echo "error: dache binary not found at $DACHE_BIN" >&2
  echo "run 'make' first" >&2
  exit 2
fi

TESTS_PASSED=0
TESTS_FAILED=0
CURRENT_TEST=""

start_test() {
  CURRENT_TEST="$1"
  echo "- $CURRENT_TEST"
}

assert_eq() {
  local expected="$1"
  local actual="$2"
  local msg="${3:-}"
  if [ "$expected" = "$actual" ]; then
    TESTS_PASSED=$((TESTS_PASSED + 1))
  else
    TESTS_FAILED=$((TESTS_FAILED + 1))
    echo "  FAIL [$CURRENT_TEST]${msg:+ $msg}: expected '$expected', got '$actual'" >&2
  fi
}

assert_file_exists() {
  local path="$1"
  if [ -e "$path" ]; then
    TESTS_PASSED=$((TESTS_PASSED + 1))
  else
    TESTS_FAILED=$((TESTS_FAILED + 1))
    echo "  FAIL [$CURRENT_TEST]: missing file: $path" >&2
  fi
}

assert_file_content() {
  local path="$1"
  local expected="$2"
  if [ ! -e "$path" ]; then
    TESTS_FAILED=$((TESTS_FAILED + 1))
    echo "  FAIL [$CURRENT_TEST]: missing file: $path" >&2
    return
  fi
  local actual
  actual="$(cat "$path")"
  if [ "$actual" = "$expected" ]; then
    TESTS_PASSED=$((TESTS_PASSED + 1))
  else
    TESTS_FAILED=$((TESTS_FAILED + 1))
    echo "  FAIL [$CURRENT_TEST]: $path: expected '$expected', got '$actual'" >&2
  fi
}

assert_contains() {
  local haystack="$1"
  local needle="$2"
  if echo "$haystack" | grep -q -- "$needle"; then
    TESTS_PASSED=$((TESTS_PASSED + 1))
  else
    TESTS_FAILED=$((TESTS_FAILED + 1))
    echo "  FAIL [$CURRENT_TEST]: output did not contain '$needle'" >&2
    echo "  ---" >&2
    echo "$haystack" | sed 's/^/  | /' >&2
    echo "  ---" >&2
  fi
}

assert_not_contains() {
  local haystack="$1"
  local needle="$2"
  if echo "$haystack" | grep -q -- "$needle"; then
    TESTS_FAILED=$((TESTS_FAILED + 1))
    echo "  FAIL [$CURRENT_TEST]: output unexpectedly contained '$needle'" >&2
  else
    TESTS_PASSED=$((TESTS_PASSED + 1))
  fi
}

# Create an isolated workspace with a private $HOME for cache isolation.
# Sets $WORKDIR (cwd) and $HOME. Caller cleans up via cleanup_workspace.
new_workspace() {
  WORKDIR="$(mktemp -d -t dache-e2e-XXXXXX)"
  export HOME="$WORKDIR/home"
  mkdir -p "$HOME"
  cd "$WORKDIR"
}

cleanup_workspace() {
  if [ -n "${WORKDIR:-}" ] && [ -d "$WORKDIR" ]; then
    cd /
    rm -rf "$WORKDIR"
  fi
}

print_summary() {
  local total=$((TESTS_PASSED + TESTS_FAILED))
  echo ""
  echo "$TESTS_PASSED/$total assertions passed"
  if [ "$TESTS_FAILED" -gt 0 ]; then
    echo "$TESTS_FAILED FAILED" >&2
    return 1
  fi
  return 0
}
