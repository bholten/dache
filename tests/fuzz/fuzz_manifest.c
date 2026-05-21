/*
 * libFuzzer harness for blob_manifest_parse — the hand-rolled JSON
 * parser that reads snapshot manifests committed to user repos. Those
 * files may be hand-edited or touched by merge tools, so any crash on
 * malformed input is a real concern.
 *
 * Build (via Makefile):
 *   make fuzz-manifest
 *
 * Run:
 *   ./build/fuzz_manifest tests/fuzz/corpus/manifest -max_total_time=60
 *
 * Interesting libFuzzer flags:
 *   -max_total_time=N    stop after N seconds (whole run)
 *   -runs=N              cap iterations
 *   -max_len=N           cap input size (default 4096)
 *   -jobs=N -workers=N   parallel fuzzing
 *
 * On a crash, libFuzzer writes the offending input to crash-<hash> in
 * cwd. Re-run that input with `./build/fuzz_manifest crash-<hash>` for
 * a deterministic reproducer.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/dache.h"

/*
 * Note on stderr: the parser fprintf's to stderr on every malformed
 * input, which clutters output across millions of iterations. We do NOT
 * silence it here — silencing breaks sanitizer crash reports too. If
 * you want a quiet run, redirect from the shell: `make fuzz-manifest
 * 2>/tmp/fuzz.log`.
 */

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  /*
   * blob_manifest_parse uses strstr/strchr internally and needs a
   * NUL-terminated buffer. The fuzz input is arbitrary bytes, so copy
   * + append our own terminator rather than touching the fuzzer's
   * read-only data.
   */
  char *buf = malloc(size + 1);

  if (!buf) {
    return 0;
  }

  if (size > 0) {
    memcpy(buf, data, size);
  }

  buf[size] = '\0';

  blob_manifest *m = blob_manifest_parse(buf);

  if (m) {
    blob_manifest_free(m);
  }

  free(buf);
  return 0;
}
