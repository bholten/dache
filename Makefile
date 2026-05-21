CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Wstrict-overflow -fno-strict-aliasing
LDFLAGS = -larchive -lcrypto -lz

LIB_SRCS = src/dache.c src/archive.c src/digest.c
SRCS = src/main.c $(LIB_SRCS)
OUT = build/dache

TEST_SRCS = tests/test_all.c
TEST_OUT = build/test_all

SAN_FLAGS = -fsanitize=address,undefined -fno-sanitize-recover=undefined \
            -fno-omit-frame-pointer -g -O1
SAN_OUT = build/dache-san
SAN_TEST_OUT = build/test_all-san

SAN_RUN_ENV = ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
              UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1

FUZZ_FLAGS = -fsanitize=fuzzer,address,undefined \
             -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g -O1
FUZZ_MANIFEST_SRC = tests/fuzz/fuzz_manifest.c
FUZZ_MANIFEST_OUT = build/fuzz_manifest

FUZZ_MANIFEST_SEEDS = tests/fuzz/corpus/manifest
FUZZ_MANIFEST_WORK = .cache/fuzz-corpus/manifest

FUZZ_SMOKE_TIME ?= 20

.PHONY: all debug release clean test e2e test-asan e2e-asan asan \
        fuzz-manifest fuzz-manifest-smoke

all: debug

debug: CFLAGS += -g -O0 -DDEBUG
debug: $(OUT)

release: CFLAGS += -O2 -DNDEBUG -s -march=native
release: $(OUT)

$(OUT): $(SRCS) | build
	$(CC) $(CFLAGS) $(SRCS) $(LDFLAGS) -o $(OUT)

$(TEST_OUT): $(TEST_SRCS) $(LIB_SRCS) | build
	$(CC) $(CFLAGS) -g -O0 $(TEST_SRCS) $(LIB_SRCS) $(LDFLAGS) -o $(TEST_OUT)

$(SAN_OUT): $(SRCS) | build
	$(CC) $(CFLAGS) $(SAN_FLAGS) $(SRCS) $(LDFLAGS) -o $(SAN_OUT)

$(SAN_TEST_OUT): $(TEST_SRCS) $(LIB_SRCS) | build
	$(CC) $(CFLAGS) $(SAN_FLAGS) $(TEST_SRCS) $(LIB_SRCS) $(LDFLAGS) -o $(SAN_TEST_OUT)

test: $(TEST_OUT)
	./$(TEST_OUT)

e2e: $(OUT)
	tests/e2e/run.sh

test-asan: $(SAN_TEST_OUT)
	$(SAN_RUN_ENV) ./$(SAN_TEST_OUT)

e2e-asan: $(SAN_OUT)
	$(SAN_RUN_ENV) DACHE_BIN=$(CURDIR)/$(SAN_OUT) tests/e2e/run.sh

asan: test-asan e2e-asan

$(FUZZ_MANIFEST_OUT): $(FUZZ_MANIFEST_SRC) $(LIB_SRCS) | build
	clang $(CFLAGS) $(FUZZ_FLAGS) $(FUZZ_MANIFEST_SRC) $(LIB_SRCS) $(LDFLAGS) \
	      -o $(FUZZ_MANIFEST_OUT)

fuzz-manifest: $(FUZZ_MANIFEST_OUT)
	@mkdir -p $(FUZZ_MANIFEST_WORK)
	$(SAN_RUN_ENV) ./$(FUZZ_MANIFEST_OUT) \
	    $(FUZZ_MANIFEST_WORK) $(FUZZ_MANIFEST_SEEDS) \
	    -max_total_time=$${FUZZ_TIME:-60}

fuzz-manifest-smoke: $(FUZZ_MANIFEST_OUT)
	@mkdir -p build/fuzz-corpus-smoke
	$(SAN_RUN_ENV) ./$(FUZZ_MANIFEST_OUT) \
	    build/fuzz-corpus-smoke $(FUZZ_MANIFEST_SEEDS) \
	    -max_total_time=$(FUZZ_SMOKE_TIME)

build:
	mkdir -p build

clean:
	rm -rf build
