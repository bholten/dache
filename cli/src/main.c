#include <openssl/sha.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compression.h"
#include "dache.h"
#include "digest.h"

void cache_key(const char** envv,
	       int envc,
	       const char** inputv,
	       int inputc,
	       const char** commandv,
	       int commandc,
	       uint8_t out_digest[32]) {
  
}

int main(int argc, const char** argv) {
  uint8_t digest[32];
  char hex[65];

  const char* envv[2] = {"path=/usr/bin/", "cc=gcc"};
  const char* inputv[2] = {"src/main.c", "src/dache.h"};
  const char* commandv[4] = {"gcc", "test.c", "-o", "test"};
  
  //dache_digest_from_file("src/main.c", digest);
  int code = dache_cache_key(envv, 2,
			     inputv, 2,
			     commandv, 4,
			     digest);
  
  if (code != 0) {
    fprintf(stderr, "[dache] error code: %i\n", code);
  }
  
  dache_digest_to_hex(digest, hex);

  printf("%s\n", hex);

  exit(0);
}
