#ifndef DACHE_H
#define DACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct dache {
  char *cache_dir;
} dache;

// Initialize dache with cache directory (NULL for default ~/.cache/dache)
dache *dache_new(const char *cache_dir);
void dache_free(dache *d);

int dache_cache_key(const char **envv, int envc, const char **inputv,
                    int inputc, const char **commandv, int commandc,
                    uint8_t out_digest[32]);

// Check if cache exists for key; if so, restore outputs and return true
bool dache_cache_get(dache *d, const char *key);

// Archive outputs and store in cache under key
bool dache_cache_put(dache *d, const char *key, const char **outputv,
                     int outputc);

// Compression
bool write_archive(const char **src, const char *dest);
bool unarchive(const char *src, const char *dest);
bool compress_file(const char *src, const char *dest);
bool decompress_file(const char *src, const char *dest);

// Digest
int digest_from_file(const char *path, uint8_t digset_out[32]);
void digest_to_hex(const uint8_t digest[32], char hex_out[65]);

// Path expansion - expands directories recursively, returns sorted file list
// Caller must free result with expanded_paths_free()
typedef struct {
  char **paths;
  int count;
  int capacity;
} expanded_paths;

expanded_paths *expand_paths(const char **pathv, int pathc);
void expanded_paths_free(expanded_paths *ep);

// Blob storage for snapshots
// Stores individual files by content hash in ~/.cache/dache/blobs/
typedef struct {
  char path[512];
  char sha256[65];
  size_t size;
  int mode;
} blob_entry;

typedef struct {
  blob_entry *entries;
  int count;
  int capacity;
} blob_manifest;

blob_manifest *blob_manifest_new(void);
void blob_manifest_free(blob_manifest *m);

// Store a file as a blob, add entry to manifest
bool blob_store(dache *d, const char *path, blob_manifest *m);

// Restore a single blob to path
bool blob_restore(dache *d, const blob_entry *entry);

// Write manifest to JSON file
bool blob_manifest_write(const blob_manifest *m, const char *path);

// Read manifest from JSON file
blob_manifest *blob_manifest_read(const char *path);

#endif
