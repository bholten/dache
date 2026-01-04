CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Wstrict-overflow -fno-strict-aliasing
LDFLAGS = -larchive -lcrypto -lz

SRCS = src/main.c src/dache.c src/archive.c src/digest.c
OUT = build/dache

.PHONY: all debug release clean

all: debug

debug: CFLAGS += -g -O0 -DDEBUG
debug: $(OUT)

release: CFLAGS += -O2 -DNDEBUG -s -march=native
release: $(OUT)

$(OUT): $(SRCS) | build
	$(CC) $(CFLAGS) $(SRCS) $(LDFLAGS) -o $(OUT)

build:
	mkdir -p build

clean:
	rm -rf build
