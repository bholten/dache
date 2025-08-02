#ifndef DACHE_H
#define DACHE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct dache dache;

int dache_cache_key(const char** envv,
		    int envc,
		    const char** inputv,
		    int inputc,
		    const char** commandv,
		    int commandc,
		    uint8_t out_digest[32]);

bool dache_cache_get(dache* d, uint8_t digest[32]);


/*
typedef struct dache {
  const char* working_directory;
  const char** objects;
  const char* trees;
  const char* manifest;
  const char* snapshot;
} dache;


void dache_init(dache* d);
void dache_get(dache* d, const char* content, unsigned const char* out);
void dache_put(dache* d, const char* content);
*/
#endif
