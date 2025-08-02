#ifndef DACHE_H
#define DACHE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct dache dache;

int dache_cache_key(const char **envv, int envc, const char **inputv,
                    int inputc, const char **commandv, int commandc,
                    uint8_t out_digest[32]);

bool dache_cache_get(dache *d, char key[64]);
bool dache_cache_put(dache *d, char key[64]);

// Compression
bool write_archive(const char **src, const char *dest);
bool unarchive(const char *src, const char *dest);
bool compress_file(const char *src, const char *dest);
bool decompress_file(const char *src, const char *dest);

// Digest
int digest_from_file(const char *path, uint8_t digset_out[32]);
void digest_to_hex(const uint8_t digest[32], char hex_out[65]);

#endif
