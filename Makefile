CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Wstrict-overflow -fno-strict-aliasing
LDFLAGS = -larchive -lcrypto -lz

LIB_SRCS = src/dache.c src/archive.c src/digest.c
SRCS = src/main.c $(LIB_SRCS)
OUT = build/dache

TEST_SRCS = tests/test_all.c
TEST_OUT = build/test_all

# AddressSanitizer + UndefinedBehaviorSanitizer build of the binaries used
# by `test-asan` / `e2e-asan`. -fno-sanitize-recover makes UB fatal (rather
# than just warnings); -O1 keeps inlining sane without hiding bugs.
SAN_FLAGS = -fsanitize=address,undefined -fno-sanitize-recover=undefined \
            -fno-omit-frame-pointer -g -O1
SAN_OUT = build/dache-san
SAN_TEST_OUT = build/test_all-san

# Leak detection (LSan) is on: the current code is leak-free under the
# tested paths, so any regression should surface as a CI failure.
SAN_RUN_ENV = ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
              UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1

.PHONY: all debug release clean test e2e test-asan e2e-asan asan

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

build:
	mkdir -p build

clean:
	rm -rf build
