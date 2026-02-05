#ifndef DACHE_H
#define DACHE_H

#include <stddef.h>

/* C89 compatibility */
#ifndef __cplusplus
typedef int bool;
#define true 1
#define false 0
#endif

typedef struct dache {
  char *cache_dir;
  char *remote_dir;
  char *hooks_dir;
} dache;

dache *dache_new(const char *cache_dir, const char *remote_dir);
void dache_free(dache *d);
int dache_cache_key(const char **envv, int envc, const char **inputv,
                    int inputc, const char **commandv, int commandc,
                    unsigned char out_digest[32]);
bool dache_cache_get(dache *d, const char *key);
bool dache_cache_put(dache *d, const char *key, const char **outputv,
                     int outputc);

bool write_archive(const char **src, const char *dest);
bool unarchive(const char *src, const char *dest);
bool compress_file(const char *src, const char *dest);
bool decompress_file(const char *src, const char *dest);

int digest_from_file(const char *path, unsigned char digest_out[32]);
void digest_to_hex(const unsigned char digest[32], char hex_out[65]);

typedef struct {
  char **paths;
  int count;
  int capacity;
} expanded_paths;

expanded_paths *expand_paths(const char **pathv, int pathc);
void expanded_paths_free(expanded_paths *ep);

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
blob_manifest *blob_manifest_read(const char *path);
bool blob_manifest_write(const blob_manifest *m, const char *path);

bool blob_store(dache *d, const char *path, blob_manifest *m);
bool blob_restore(dache *d, const blob_entry *entry);

#endif
