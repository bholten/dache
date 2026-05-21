#ifndef DACHE_H
#define DACHE_H

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
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
                    uint8_t out_digest[32]);
bool dache_cache_get(dache *d, const char *key);
bool dache_cache_put(dache *d, const char *key, const char **outputv,
                     int outputc);

bool write_archive(const char **src, const char *dest);
bool unarchive(const char *src, const char *dest);
bool compress_file(const char *src, const char *dest);
bool decompress_file(const char *src, const char *dest);

int digest_from_file(const char *path, uint8_t digest_out[32]);
void digest_to_hex(const uint8_t digest[32], char hex_out[65]);

typedef struct {
  char **paths;
  int count;
  int capacity;
} expanded_paths;

expanded_paths *expand_paths(const char **pathv, int pathc);
void expanded_paths_free(expanded_paths *ep);

/*
 * blob_entry.path is fixed-size for now; widening to dynamic / PATH_MAX
 * is tracked in TODO.md. The static_assert keeps us honest about how
 * little headroom we have versus a real POSIX path.
 */
typedef struct {
  char path[512];
  char sha256[65];
  size_t size;
  int mode;
} blob_entry;

static_assert(sizeof(((blob_entry *)0)->path) <= PATH_MAX,
              "blob_entry.path must fit within PATH_MAX");
static_assert(sizeof(((blob_entry *)0)->sha256) == 65,
              "blob_entry.sha256 must hold 64 hex chars + NUL");

typedef struct {
  blob_entry *entries;
  int count;
  int capacity;
} blob_manifest;

blob_manifest *blob_manifest_new(void);
void blob_manifest_free(blob_manifest *m);
blob_manifest *blob_manifest_read(const char *path);

blob_manifest *blob_manifest_parse(const char *json);
bool blob_manifest_write(const blob_manifest *m, const char *path);

bool blob_store(dache *d, const char *path, blob_manifest *m);
bool blob_restore(dache *d, const blob_entry *entry);

#endif
