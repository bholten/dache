# Manifest fuzz corpus

Seed inputs for `tests/fuzz/fuzz_manifest.c`. Kept deliberately small —
each file represents one path through `blob_manifest_parse` that's worth
exercising from a cold start.

The two `regression_*.json` files are pinned reproducers for bugs the
fuzzer found and we then fixed. If a future change reintroduces either
crash, libFuzzer hits it on the very first iteration when it runs the
seed corpus before mutating. Don't delete them.

The working corpus (where libFuzzer accumulates mutation-derived
coverage) lives at `.cache/fuzz-corpus/manifest/` and is gitignored —
see the `fuzz-manifest` Makefile target.
