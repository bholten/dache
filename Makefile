CC = gcc
CFLAGS = -std=c89 -Wall -Wextra -Wpedantic -Wstrict-overflow -fno-strict-aliasing
LDFLAGS = -larchive -lcrypto -lz

LIB_SRCS = src/dache.c src/archive.c src/digest.c
SRCS = src/main.c $(LIB_SRCS)
OUT = build/dache

TEST_SRCS = tests/test_all.c
TEST_OUT = build/test_all

.PHONY: all debug release clean test e2e

all: debug

debug: CFLAGS += -g -O0 -DDEBUG
debug: $(OUT)

release: CFLAGS += -O2 -DNDEBUG -s -march=native
release: $(OUT)

$(OUT): $(SRCS) | build
	$(CC) $(CFLAGS) $(SRCS) $(LDFLAGS) -o $(OUT)

$(TEST_OUT): $(TEST_SRCS) $(LIB_SRCS) | build
	$(CC) $(CFLAGS) -g -O0 $(TEST_SRCS) $(LIB_SRCS) $(LDFLAGS) -o $(TEST_OUT)

test: $(TEST_OUT)
	./$(TEST_OUT)

e2e: $(OUT)
	tests/e2e/run.sh

build:
	mkdir -p build

clean:
	rm -rf build
