#!/usr/bin/env bash
# Run all e2e tests; aggregate pass/fail.

set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

total_failed=0
total_run=0

for t in "$SCRIPT_DIR"/test_*.sh; do
  name="$(basename "$t")"
  echo "=== $name ==="
  if bash "$t"; then
    :
  else
    total_failed=$((total_failed + 1))
  fi
  total_run=$((total_run + 1))
  echo ""
done

echo "============================="
echo "ran $total_run e2e test files"
if [ "$total_failed" -gt 0 ]; then
  echo "FAILED: $total_failed file(s)" >&2
  exit 1
fi
echo "ALL PASSED"
