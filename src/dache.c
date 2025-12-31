#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <openssl/evp.h>

#include "dache.h"

#define DEFAULT_CACHE_DIR ".cache/dache"

static char *get_default_cache_dir(void) {
  const char *home = getenv("HOME");
  if (!home) return NULL;

  size_t len = strlen(home) + 1 + strlen(DEFAULT_CACHE_DIR) + 1;
  char *path = malloc(len);
  if (!path) return NULL;

  snprintf(path, len, "%s/%s", home, DEFAULT_CACHE_DIR);
  return path;
}

static bool ensure_dir_exists(const char *path) {
  struct stat st;
  if (stat(path, &st) == 0) {
    return S_ISDIR(st.st_mode);
  }
  // Create directory (and parent if needed)
  // Simple approach: just try mkdir, assume parent exists
  return mkdir(path, 0755) == 0;
}

dache *dache_new(const char *cache_dir) {
  dache *d = malloc(sizeof(dache));
  if (!d) return NULL;

  if (cache_dir) {
    d->cache_dir = strdup(cache_dir);
  } else {
    d->cache_dir = get_default_cache_dir();
  }

  if (!d->cache_dir) {
    free(d);
    return NULL;
  }

  // Ensure cache directory exists
  if (!ensure_dir_exists(d->cache_dir)) {
    // Try creating parent first (e.g., ~/.cache)
    char *parent = strdup(d->cache_dir);
    char *last_slash = strrchr(parent, '/');
    if (last_slash) {
      *last_slash = '\0';
      ensure_dir_exists(parent);
      ensure_dir_exists(d->cache_dir);
    }
    free(parent);
  }

  return d;
}

void dache_free(dache *d) {
  if (d) {
    free(d->cache_dir);
    free(d);
  }
}

static int digest(EVP_MD_CTX *ctx, const char *path) {
  FILE *f = fopen(path, "rb");

  if (!f) {
    fprintf(stderr, "[dache] could not open file: %s\n", path);
    return -1;
  }

  uint8_t buff[65536];
  size_t n;

  while ((n = fread(buff, 1, sizeof(buff), f)) > 0) {
    if (!EVP_DigestUpdate(ctx, buff, n)) {
      fclose(f);
      return -4;
    }
  }

  fclose(f);
  return 0;
}

int dache_cache_key(const char **envv, int envc, const char **inputv,
                    int inputc, const char **commandv, int commandc,
                    uint8_t out_digest[32]) {
  EVP_MD_CTX *ctx = EVP_MD_CTX_new();

  if (!ctx) {
    return -1;
  }

  const EVP_MD *md = EVP_sha256();

  if (!EVP_DigestInit_ex(ctx, md, NULL)) {
    EVP_MD_CTX_free(ctx);
    return -2;
  }

  if (!EVP_DigestUpdate(ctx, "env:\n", 5)) {
    EVP_MD_CTX_free(ctx);
    return -3;
  }

  for (int i = 0; i < envc; i++) {
    if (!EVP_DigestUpdate(ctx, envv[i], strlen(envv[i]))) {
      EVP_MD_CTX_free(ctx);
      return -4;
    }

    if (!EVP_DigestUpdate(ctx, "\n", 1)) {
      EVP_MD_CTX_free(ctx);
      return -5;
    }
  }

  if (!EVP_DigestUpdate(ctx, "inputs:\n", 8)) {
    EVP_MD_CTX_free(ctx);
    return -6;
  }

  for (int j = 0; j < inputc; j++) {
    int code = digest(ctx, inputv[j]);

    if (code != 0) {
      fprintf(stderr, "[dache] failed to hash input: %s\n", inputv[j]);
      EVP_MD_CTX_free(ctx);
      return -7;
    }
  }

  if (!EVP_DigestUpdate(ctx, "\n", 1)) {
    EVP_MD_CTX_free(ctx);
    return -8;
  }

  if (!EVP_DigestUpdate(ctx, "command:\n", 9)) {
    EVP_MD_CTX_free(ctx);
    return -9;
  }

  for (int k = 0; k < commandc; k++) {
    if (!EVP_DigestUpdate(ctx, commandv[k], strlen(commandv[k]))) {
      EVP_MD_CTX_free(ctx);
      return -10;
    }
  }

  if (!EVP_DigestUpdate(ctx, "\n", 1)) {
    EVP_MD_CTX_free(ctx);
    return -11;
  }

  unsigned int len = 0;

  if (!EVP_DigestFinal_ex(ctx, out_digest, &len)) {
    EVP_MD_CTX_free(ctx);
    return -12;
  }

  EVP_MD_CTX_free(ctx);
  return 0;
}

static char *cache_path_for_key(dache *d, const char *key) {
  // <cache_dir>/<key>.tar.gz
  size_t len = strlen(d->cache_dir) + 1 + 64 + 7 + 1; // 64 hex + ".tar.gz"
  char *path = malloc(len);
  if (!path) return NULL;
  snprintf(path, len, "%s/%s.tar.gz", d->cache_dir, key);
  return path;
}

bool dache_cache_get(dache *d, const char *key) {
  if (!d || !key) return false;

  char *path = cache_path_for_key(d, key);
  if (!path) return false;

  // Check if cache file exists
  if (access(path, F_OK) != 0) {
    free(path);
    return false;
  }

  // Cache hit - extract archive to current directory
  bool ok = unarchive(path, ".");
  free(path);

  if (ok) {
    fprintf(stderr, "[dache] cache hit: %s\n", key);
  }

  return ok;
}

bool dache_cache_put(dache *d, const char *key, const char **outputv,
                     int outputc) {
  if (!d || !key || !outputv || outputc <= 0) return false;

  char *path = cache_path_for_key(d, key);
  if (!path) return false;

  // Build null-terminated array for write_archive
  const char **srcs = malloc((outputc + 1) * sizeof(char *));
  if (!srcs) {
    free(path);
    return false;
  }

  for (int i = 0; i < outputc; i++) {
    srcs[i] = outputv[i];
  }
  srcs[outputc] = NULL;

  bool ok = write_archive(srcs, path);
  free(srcs);

  if (ok) {
    fprintf(stderr, "[dache] cached: %s\n", key);
  } else {
    fprintf(stderr, "[dache] failed to cache: %s\n", key);
  }

  free(path);
  return ok;
}

// Path expansion implementation

static expanded_paths *expanded_paths_new(void) {
  expanded_paths *ep = malloc(sizeof(expanded_paths));
  if (!ep) return NULL;

  ep->capacity = 64;
  ep->count = 0;
  ep->paths = malloc(ep->capacity * sizeof(char *));
  if (!ep->paths) {
    free(ep);
    return NULL;
  }
  return ep;
}

static bool expanded_paths_add(expanded_paths *ep, const char *path) {
  if (ep->count >= ep->capacity) {
    int new_cap = ep->capacity * 2;
    char **new_paths = realloc(ep->paths, new_cap * sizeof(char *));
    if (!new_paths) return false;
    ep->paths = new_paths;
    ep->capacity = new_cap;
  }

  ep->paths[ep->count] = strdup(path);
  if (!ep->paths[ep->count]) return false;
  ep->count++;
  return true;
}

static bool expand_path_recursive(expanded_paths *ep, const char *path) {
  struct stat st;
  if (stat(path, &st) != 0) {
    fprintf(stderr, "[dache] warning: cannot stat '%s'\n", path);
    return false;
  }

  if (S_ISREG(st.st_mode)) {
    return expanded_paths_add(ep, path);
  }

  if (S_ISDIR(st.st_mode)) {
    DIR *dir = opendir(path);
    if (!dir) {
      fprintf(stderr, "[dache] warning: cannot open directory '%s'\n", path);
      return false;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
      // Skip . and ..
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
        continue;
      }

      // Build full path
      size_t path_len = strlen(path);
      size_t name_len = strlen(entry->d_name);
      size_t full_len = path_len + 1 + name_len + 1;
      char *full_path = malloc(full_len);
      if (!full_path) {
        closedir(dir);
        return false;
      }

      // Handle trailing slash
      if (path[path_len - 1] == '/') {
        snprintf(full_path, full_len, "%s%s", path, entry->d_name);
      } else {
        snprintf(full_path, full_len, "%s/%s", path, entry->d_name);
      }

      expand_path_recursive(ep, full_path);
      free(full_path);
    }

    closedir(dir);
    return true;
  }

  // Skip other file types (symlinks, devices, etc.)
  return true;
}

static int path_compare(const void *a, const void *b) {
  return strcmp(*(const char **)a, *(const char **)b);
}

expanded_paths *expand_paths(const char **pathv, int pathc) {
  expanded_paths *ep = expanded_paths_new();
  if (!ep) return NULL;

  for (int i = 0; i < pathc; i++) {
    expand_path_recursive(ep, pathv[i]);
  }

  // Sort for deterministic ordering
  if (ep->count > 1) {
    qsort(ep->paths, ep->count, sizeof(char *), path_compare);
  }

  return ep;
}

void expanded_paths_free(expanded_paths *ep) {
  if (!ep) return;
  for (int i = 0; i < ep->count; i++) {
    free(ep->paths[i]);
  }
  free(ep->paths);
  free(ep);
}

// Blob storage implementation

static char *get_blob_dir(dache *d) {
  size_t len = strlen(d->cache_dir) + strlen("/blobs") + 1;
  char *path = malloc(len);
  if (!path) return NULL;
  snprintf(path, len, "%s/blobs", d->cache_dir);
  return path;
}

static char *get_blob_path(dache *d, const char *sha256) {
  size_t len = strlen(d->cache_dir) + strlen("/blobs/") + 64 + 1;
  char *path = malloc(len);
  if (!path) return NULL;
  snprintf(path, len, "%s/blobs/%s", d->cache_dir, sha256);
  return path;
}

blob_manifest *blob_manifest_new(void) {
  blob_manifest *m = malloc(sizeof(blob_manifest));
  if (!m) return NULL;

  m->capacity = 64;
  m->count = 0;
  m->entries = malloc(m->capacity * sizeof(blob_entry));
  if (!m->entries) {
    free(m);
    return NULL;
  }
  return m;
}

void blob_manifest_free(blob_manifest *m) {
  if (!m) return;
  free(m->entries);
  free(m);
}

static bool blob_manifest_add(blob_manifest *m, const blob_entry *entry) {
  if (m->count >= m->capacity) {
    int new_cap = m->capacity * 2;
    blob_entry *new_entries = realloc(m->entries, new_cap * sizeof(blob_entry));
    if (!new_entries) return false;
    m->entries = new_entries;
    m->capacity = new_cap;
  }
  m->entries[m->count++] = *entry;
  return true;
}

static bool copy_file(const char *src, const char *dest) {
  FILE *in = fopen(src, "rb");
  if (!in) return false;

  FILE *out = fopen(dest, "wb");
  if (!out) {
    fclose(in);
    return false;
  }

  char buf[65536];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
    if (fwrite(buf, 1, n, out) != n) {
      fclose(in);
      fclose(out);
      return false;
    }
  }

  fclose(in);
  fclose(out);
  return true;
}

bool blob_store(dache *d, const char *path, blob_manifest *m) {
  if (!d || !path || !m) return false;

  // Get file info
  struct stat st;
  if (stat(path, &st) != 0) {
    fprintf(stderr, "[dache] cannot stat: %s\n", path);
    return false;
  }

  if (!S_ISREG(st.st_mode)) {
    fprintf(stderr, "[dache] not a regular file: %s\n", path);
    return false;
  }

  // Hash the file
  uint8_t digest[32];
  if (digest_from_file(path, digest) != 0) {
    fprintf(stderr, "[dache] failed to hash: %s\n", path);
    return false;
  }

  blob_entry entry;
  strncpy(entry.path, path, sizeof(entry.path) - 1);
  entry.path[sizeof(entry.path) - 1] = '\0';
  digest_to_hex(digest, entry.sha256);
  entry.size = st.st_size;
  entry.mode = st.st_mode & 0777;

  // Ensure blob directory exists
  char *blob_dir = get_blob_dir(d);
  if (!blob_dir) return false;
  ensure_dir_exists(blob_dir);
  free(blob_dir);

  // Check if blob already exists (deduplication)
  char *blob_path = get_blob_path(d, entry.sha256);
  if (!blob_path) return false;

  if (access(blob_path, F_OK) != 0) {
    // Blob doesn't exist, copy it
    if (!copy_file(path, blob_path)) {
      fprintf(stderr, "[dache] failed to store blob: %s\n", path);
      free(blob_path);
      return false;
    }
  }
  free(blob_path);

  // Add to manifest
  return blob_manifest_add(m, &entry);
}

bool blob_restore(dache *d, const blob_entry *entry) {
  if (!d || !entry) return false;

  char *blob_path = get_blob_path(d, entry->sha256);
  if (!blob_path) return false;

  // Check blob exists
  if (access(blob_path, F_OK) != 0) {
    fprintf(stderr, "[dache] blob not found: %s\n", entry->sha256);
    free(blob_path);
    return false;
  }

  // Create parent directories if needed
  char *path_copy = strdup(entry->path);
  if (path_copy) {
    char *last_slash = strrchr(path_copy, '/');
    if (last_slash) {
      *last_slash = '\0';
      // Simple mkdir -p equivalent
      char *p = path_copy;
      while (*p) {
        if (*p == '/') {
          *p = '\0';
          mkdir(path_copy, 0755);
          *p = '/';
        }
        p++;
      }
      mkdir(path_copy, 0755);
    }
    free(path_copy);
  }

  // Copy blob to destination
  if (!copy_file(blob_path, entry->path)) {
    fprintf(stderr, "[dache] failed to restore: %s\n", entry->path);
    free(blob_path);
    return false;
  }

  // Restore permissions
  chmod(entry->path, entry->mode);

  free(blob_path);
  return true;
}

// Simple JSON writing (no external dependency)
bool blob_manifest_write(const blob_manifest *m, const char *path) {
  if (!m || !path) return false;

  FILE *f = fopen(path, "w");
  if (!f) return false;

  fprintf(f, "{\n");
  fprintf(f, "  \"version\": 1,\n");
  fprintf(f, "  \"type\": \"individual\",\n");
  fprintf(f, "  \"files\": [\n");

  for (int i = 0; i < m->count; i++) {
    const blob_entry *e = &m->entries[i];
    fprintf(f, "    {\n");
    fprintf(f, "      \"path\": \"%s\",\n", e->path);
    fprintf(f, "      \"sha256\": \"%s\",\n", e->sha256);
    fprintf(f, "      \"size\": %zu,\n", e->size);
    fprintf(f, "      \"mode\": %d\n", e->mode);
    fprintf(f, "    }%s\n", (i < m->count - 1) ? "," : "");
  }

  fprintf(f, "  ]\n");
  fprintf(f, "}\n");

  fclose(f);
  return true;
}

// Simple JSON parsing (minimal, handles our specific format)
static char *json_read_file(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f) return NULL;

  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);

  char *buf = malloc(len + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }

  fread(buf, 1, len, f);
  buf[len] = '\0';
  fclose(f);
  return buf;
}

static const char *json_skip_ws(const char *p) {
  while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
    p++;
  return p;
}

static const char *json_parse_string(const char *p, char *out,
                                     size_t out_size) {
  p = json_skip_ws(p);
  if (*p != '"') return NULL;
  p++;

  size_t i = 0;
  while (*p && *p != '"' && i < out_size - 1) {
    if (*p == '\\' && *(p + 1)) {
      p++;
    }
    out[i++] = *p++;
  }
  out[i] = '\0';

  if (*p == '"') p++;
  return p;
}

static const char *json_parse_number(const char *p, long *out) {
  p = json_skip_ws(p);
  char *end;
  *out = strtol(p, &end, 10);
  return end;
}

blob_manifest *blob_manifest_read(const char *path) {
  char *json = json_read_file(path);
  if (!json) return NULL;

  blob_manifest *m = blob_manifest_new();
  if (!m) {
    free(json);
    return NULL;
  }

  const char *p = json;

  // Find "files" array
  p = strstr(p, "\"files\"");
  if (!p) {
    free(json);
    blob_manifest_free(m);
    return NULL;
  }

  p = strchr(p, '[');
  if (!p) {
    free(json);
    blob_manifest_free(m);
    return NULL;
  }
  p++;

  // Parse each file entry
  while (*p) {
    p = json_skip_ws(p);
    if (*p == ']') break;
    if (*p == ',') {
      p++;
      continue;
    }
    if (*p != '{') {
      p++;
      continue;
    }
    p++;

    blob_entry entry = {0};

    // Parse fields
    while (*p && *p != '}') {
      p = json_skip_ws(p);
      if (*p == '}') break;
      if (*p == ',') {
        p++;
        continue;
      }
      if (*p != '"') {
        p++;
        continue;
      }

      char key[64];
      p = json_parse_string(p, key, sizeof(key));
      if (!p) break;

      p = json_skip_ws(p);
      if (*p == ':') p++;
      p = json_skip_ws(p);

      if (strcmp(key, "path") == 0) {
        p = json_parse_string(p, entry.path, sizeof(entry.path));
      } else if (strcmp(key, "sha256") == 0) {
        p = json_parse_string(p, entry.sha256, sizeof(entry.sha256));
      } else if (strcmp(key, "size") == 0) {
        long val;
        p = json_parse_number(p, &val);
        entry.size = val;
      } else if (strcmp(key, "mode") == 0) {
        long val;
        p = json_parse_number(p, &val);
        entry.mode = (int)val;
      }
    }

    if (*p == '}') p++;

    if (entry.path[0] && entry.sha256[0]) {
      blob_manifest_add(m, &entry);
    }
  }

  free(json);
  return m;
}
