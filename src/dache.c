#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <openssl/evp.h>

#include "dache.h"

#define DEFAULT_CACHE_DIR ".cache/dache"
#define DEFAULT_HOOKS_DIR ".config/dache/hooks"

/*
 * On-disk format version for the snapshot manifest JSON. Bump only
 * when the schema changes in a way old readers can't safely
 * parse. The reader rejects any other value rather than guessing.
 */
#define MANIFEST_VERSION 1

static char *get_default_cache_dir(void) {
  const char *home = getenv("HOME");

  if (!home) {
    return NULL;
  }

  size_t len = strlen(home) + 1 + strlen(DEFAULT_CACHE_DIR) + 1;
  char *path = malloc(len);

  if (!path) {
    return NULL;
  }

  snprintf(path, len, "%s/%s", home, DEFAULT_CACHE_DIR);

  return path;
}

static char *get_default_hooks_dir(void) {
  const char *home = getenv("HOME");

  if (!home) {
    return NULL;
  }

  size_t len = strlen(home) + 1 + strlen(DEFAULT_HOOKS_DIR) + 1;
  char *path = malloc(len);

  if (!path) {
    return NULL;
  }

  snprintf(path, len, "%s/%s", home, DEFAULT_HOOKS_DIR);

  return path;
}

static bool mkdir_p(const char *path) {
  char *p;
  struct stat st;

  if (stat(path, &st) == 0) {
    return S_ISDIR(st.st_mode);
  }

  char *copy = strdup(path);

  if (!copy) {
    return false;
  }

  bool ok = true;

  for (p = copy + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';

      if (mkdir(copy, 0755) != 0 && stat(copy, &st) != 0) {
        ok = false;
        break;
      }

      *p = '/';
    }
  }

  if (ok && mkdir(copy, 0755) != 0 &&
      (stat(copy, &st) != 0 || !S_ISDIR(st.st_mode))) {
    ok = false;
  }

  free(copy);
  return ok;
}

dache *dache_new(const char *cache_dir, const char *remote_dir) {
  dache *d = malloc(sizeof(dache));

  if (!d) {
    return NULL;
  }

  if (cache_dir) {
    d->cache_dir = strdup(cache_dir);
  } else {
    d->cache_dir = get_default_cache_dir();
  }

  if (!d->cache_dir) {
    free(d);
    return NULL;
  }

  d->remote_dir = remote_dir ? strdup(remote_dir) : NULL;
  d->hooks_dir = get_default_hooks_dir();

  if (!mkdir_p(d->cache_dir)) {
    fprintf(stderr, "[dache] cannot create cache dir: %s\n", d->cache_dir);
    dache_free(d);
    return NULL;
  }

  return d;
}

void dache_free(dache *d) {
  if (d) {
    free(d->cache_dir);
    free(d->remote_dir);
    free(d->hooks_dir);
    free(d);
  }
}

static bool copy_file(const char *src, const char *dest);

static char *get_hook_path(dache *d, const char *hook_name) {
  if (!d->hooks_dir) {
    return NULL;
  }

  size_t len = strlen(d->hooks_dir) + 1 + strlen(hook_name) + 1;
  char *path = malloc(len);

  if (!path) {
    return NULL;
  }

  snprintf(path, len, "%s/%s", d->hooks_dir, hook_name);

  return path;
}

static bool hook_exists(dache *d, const char *hook_name) {
  char *path = get_hook_path(d, hook_name);

  if (!path) {
    return false;
  }

  bool exists = access(path, X_OK) == 0;
  free(path);

  return exists;
}

static bool run_hook(dache *d, const char *hook_name, const char *arg1,
                     const char *arg2) {
  char *argv[4];
  int status;

  char *hook_path = get_hook_path(d, hook_name);

  if (!hook_path) {
    return false;
  }

  if (access(hook_path, X_OK) != 0) {
    free(hook_path);
    return false;
  }

  pid_t pid = fork();

  if (pid < 0) {
    free(hook_path);
    return false;
  }

  if (pid == 0) {
    argv[0] = hook_path;
    argv[1] = (char *)arg1;
    argv[2] = (char *)arg2;
    argv[3] = NULL;
    execvp(argv[0], argv);
    _exit(127);
  }

  free(hook_path);

  if (waitpid(pid, &status, 0) < 0) {
    return false;
  }

  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static bool remote_file_get(dache *d, const char *remote_path,
                            const char *local_path) {
  if (!d->remote_dir) {
    return false;
  }

  size_t len = strlen(d->remote_dir) + 1 + strlen(remote_path) + 1;
  char *full_remote = malloc(len);

  if (!full_remote) {
    return false;
  }

  snprintf(full_remote, len, "%s/%s", d->remote_dir, remote_path);

  if (access(full_remote, F_OK) != 0) {
    free(full_remote);
    return false;
  }

  bool ok = copy_file(full_remote, local_path);
  free(full_remote);

  return ok;
}

static bool remote_file_put(dache *d, const char *local_path,
                            const char *remote_path) {
  if (!d->remote_dir) {
    return false;
  }

  size_t len = strlen(d->remote_dir) + 1 + strlen(remote_path) + 1;
  char *full_remote = malloc(len);

  if (!full_remote) {
    return false;
  }

  snprintf(full_remote, len, "%s/%s", d->remote_dir, remote_path);
  char *parent = strdup(full_remote);
  char *last_slash = strrchr(parent, '/');

  if (last_slash) {
    *last_slash = '\0';
    mkdir_p(parent);
  }

  free(parent);

  bool ok = copy_file(local_path, full_remote);
  free(full_remote);

  return ok;
}

/*
 * Each section item is length-prefixed ("N:" + payload + "\n") so that
 * adjacent items can't be ambiguously concatenated into a colliding key.
 */
static bool digest_framed(EVP_MD_CTX *ctx, const void *data, size_t n) {
  char header[32];
  int hlen = snprintf(header, sizeof(header), "%lu:", (unsigned long)n);

  if (hlen < 0 || (size_t)hlen >= sizeof(header)) {
    return false;
  }

  if (!EVP_DigestUpdate(ctx, header, (size_t)hlen)) {
    return false;
  }

  if (n > 0 && !EVP_DigestUpdate(ctx, data, n)) {
    return false;
  }

  if (!EVP_DigestUpdate(ctx, "\n", 1)) {
    return false;
  }

  return true;
}

static int digest_file_framed(EVP_MD_CTX *ctx, const char *path) {
  struct stat st;
  uint8_t buff[65536];
  char header[64];
  size_t n;
  size_t remaining;

  if (stat(path, &st) != 0) {
    fprintf(stderr, "[dache] could not stat input: %s\n", path);
    return -1;
  }

  int hlen =
      snprintf(header, sizeof(header), "%lu:", (unsigned long)st.st_size);

  if (hlen < 0 || (size_t)hlen >= sizeof(header)) {
    return -2;
  }

  if (!EVP_DigestUpdate(ctx, header, (size_t)hlen)) {
    return -3;
  }

  FILE *f = fopen(path, "rb");

  if (!f) {
    fprintf(stderr, "[dache] could not open input: %s\n", path);
    return -4;
  }

  remaining = (size_t)st.st_size;

  while (remaining > 0) {
    n = fread(buff, 1, sizeof(buff), f);

    if (n == 0) {
      break;
    }

    if (n > remaining) {
      n = remaining;
    }

    if (!EVP_DigestUpdate(ctx, buff, n)) {
      fclose(f);
      return -5;
    }

    remaining -= n;
  }

  if (ferror(f)) {
    fclose(f);
    return -6;
  }

  fclose(f);

  if (remaining != 0) {
    fprintf(stderr, "[dache] input file shrank during hashing: %s\n", path);
    return -7;
  }

  if (!EVP_DigestUpdate(ctx, "\n", 1)) {
    return -8;
  }

  return 0;
}

static int strptr_compare(const void *a, const void *b);

int dache_cache_key(const char **envv, int envc, const char **inputv,
                    int inputc, const char **commandv, int commandc,
                    uint8_t out_digest[32]) {
  EVP_MD_CTX *ctx = EVP_MD_CTX_new();

  if (!ctx) {
    return -1;
  }

  if (!EVP_DigestInit_ex(ctx, EVP_sha256(), NULL)) {
    EVP_MD_CTX_free(ctx);
    return -2;
  }

  if (!EVP_DigestUpdate(ctx, "dache-key:v1\n", 13)) {
    EVP_MD_CTX_free(ctx);
    return -3;
  }

  const char **env_sorted = NULL;

  if (envc > 0) {
    env_sorted = malloc((size_t)envc * sizeof(char *));

    if (!env_sorted) {
      EVP_MD_CTX_free(ctx);
      return -4;
    }

    for (int i = 0; i < envc; i++) {
      env_sorted[i] = envv[i];
    }

    qsort(env_sorted, (size_t)envc, sizeof(char *), strptr_compare);
  }

  if (!EVP_DigestUpdate(ctx, "env:\n", 5)) {
    free(env_sorted);
    EVP_MD_CTX_free(ctx);
    return -5;
  }

  for (int i = 0; i < envc; i++) {
    if (!digest_framed(ctx, env_sorted[i], strlen(env_sorted[i]))) {
      free(env_sorted);
      EVP_MD_CTX_free(ctx);
      return -6;
    }
  }

  free(env_sorted);

  if (!EVP_DigestUpdate(ctx, "inputs:\n", 8)) {
    EVP_MD_CTX_free(ctx);
    return -7;
  }

  for (int i = 0; i < inputc; i++) {
    if (!digest_framed(ctx, inputv[i], strlen(inputv[i]))) {
      EVP_MD_CTX_free(ctx);
      return -8;
    }
    int code = digest_file_framed(ctx, inputv[i]);
    if (code != 0) {
      fprintf(stderr, "[dache] failed to hash input: %s\n", inputv[i]);
      EVP_MD_CTX_free(ctx);
      return -9;
    }
  }

  if (!EVP_DigestUpdate(ctx, "command:\n", 9)) {
    EVP_MD_CTX_free(ctx);
    return -10;
  }

  for (int i = 0; i < commandc; i++) {
    if (!digest_framed(ctx, commandv[i], strlen(commandv[i]))) {
      EVP_MD_CTX_free(ctx);
      return -11;
    }
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
  /* <cache_dir>/<key>.tar.gz : 64 hex + ".tar.gz" */
  size_t len = strlen(d->cache_dir) + 1 + 64 + 7 + 1;
  char *path = malloc(len);

  if (!path) {
    return NULL;
  }

  snprintf(path, len, "%s/%s.tar.gz", d->cache_dir, key);

  return path;
}

bool dache_cache_get(dache *d, const char *key) {
  if (!d || !key) {
    return false;
  }

  char *path = cache_path_for_key(d, key);

  if (!path) {
    return false;
  }

  if (access(path, F_OK) == 0) {
    bool ok = unarchive(path, ".");

    if (ok) {
      fprintf(stderr, "[dache] cache hit (local): %s\n", key);
    }

    free(path);
    return ok;
  }

  if (hook_exists(d, "remote-get") && run_hook(d, "remote-get", key, path) &&
      access(path, F_OK) == 0) {
    bool ok = unarchive(path, ".");

    if (ok) {
      fprintf(stderr, "[dache] cache hit (hook): %s\n", key);
    }

    free(path);
    return ok;
  }

  char remote_key[128];
  snprintf(remote_key, sizeof(remote_key), "%s.tar.gz", key);

  if (remote_file_get(d, remote_key, path)) {
    bool ok = unarchive(path, ".");

    if (ok) {
      fprintf(stderr, "[dache] cache hit (remote): %s\n", key);
    }

    free(path);
    return ok;
  }

  free(path);
  return false;
}

bool dache_cache_put(dache *d, const char *key, const char **outputv,
                     int outputc) {
  if (!d || !key || !outputv || outputc <= 0) {
    return false;
  }

  char *path = cache_path_for_key(d, key);

  if (!path) {
    return false;
  }

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

  if (!ok) {
    fprintf(stderr, "[dache] failed to cache: %s\n", key);
    free(path);
    return false;
  }

  fprintf(stderr, "[dache] cached (local): %s\n", key);

  if (hook_exists(d, "remote-put")) {
    if (run_hook(d, "remote-put", key, path)) {
      fprintf(stderr, "[dache] pushed to remote (hook)\n");
    }
  } else if (d->remote_dir) {
    char remote_key[128];
    snprintf(remote_key, sizeof(remote_key), "%s.tar.gz", key);

    if (remote_file_put(d, path, remote_key)) {
      fprintf(stderr, "[dache] pushed to remote (file://)\n");
    }
  }

  free(path);
  return true;
}

static expanded_paths *expanded_paths_new(void) {
  expanded_paths *ep;

  ep = malloc(sizeof(expanded_paths));

  if (!ep) {
    return NULL;
  }

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
  int new_cap;
  char **new_paths;

  if (ep->count >= ep->capacity) {
    new_cap = ep->capacity * 2;
    new_paths = realloc(ep->paths, new_cap * sizeof(char *));

    if (!new_paths) {
      return false;
    }

    ep->paths = new_paths;
    ep->capacity = new_cap;
  }

  ep->paths[ep->count] = strdup(path);

  if (!ep->paths[ep->count]) {
    return false;
  }

  ep->count++;

  return true;
}

static bool expand_path_recursive(expanded_paths *ep, const char *path) {
  struct stat st;

  if (lstat(path, &st) != 0) {
    fprintf(stderr, "[dache] warning: cannot stat '%s'\n", path);
    return false;
  }

  if (S_ISLNK(st.st_mode)) {
    fprintf(stderr, "[dache] warning: skipping symlink '%s'\n", path);
    return true;
  }

  if (S_ISREG(st.st_mode)) {
    return expanded_paths_add(ep, path);
  }

  if (!S_ISDIR(st.st_mode)) {
    return true;
  }

  DIR *dir = opendir(path);

  if (!dir) {
    fprintf(stderr, "[dache] warning: cannot open directory '%s'\n", path);
    return false;
  }

  size_t path_len = strlen(path);
  bool trailing_slash = path_len > 0 && path[path_len - 1] == '/';

  struct dirent *entry;

  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    size_t full_len = path_len + 1 + strlen(entry->d_name) + 1;
    char *full_path = malloc(full_len);

    if (!full_path) {
      closedir(dir);
      return false;
    }

    snprintf(full_path, full_len, "%s%s%s", path, trailing_slash ? "" : "/",
             entry->d_name);
    expand_path_recursive(ep, full_path);
    free(full_path);
  }

  closedir(dir);
  return true;
}

static int strptr_compare(const void *a, const void *b) {
  return strcmp(*(const char **)a, *(const char **)b);
}

expanded_paths *expand_paths(const char **pathv, int pathc) {
  expanded_paths *ep = expanded_paths_new();

  if (!ep) {
    return NULL;
  }

  for (int i = 0; i < pathc; i++) {
    expand_path_recursive(ep, pathv[i]);
  }

  if (ep->count > 1) {
    qsort(ep->paths, ep->count, sizeof(char *), strptr_compare);
  }

  return ep;
}

void expanded_paths_free(expanded_paths *ep) {
  int i;

  if (!ep) {
    return;
  }

  for (i = 0; i < ep->count; i++) {
    free(ep->paths[i]);
  }

  free(ep->paths);
  free(ep);
}

static char *get_blob_dir(dache *d) {
  size_t len;
  char *path;

  len = strlen(d->cache_dir) + strlen("/blobs") + 1;
  path = malloc(len);

  if (!path) {
    return NULL;
  }

  snprintf(path, len, "%s/blobs", d->cache_dir);

  return path;
}

static char *get_blob_path(dache *d, const char *sha256) {
  size_t len;
  char *path;

  len = strlen(d->cache_dir) + strlen("/blobs/") + 64 + 1;
  path = malloc(len);

  if (!path) {
    return NULL;
  }

  snprintf(path, len, "%s/blobs/%s", d->cache_dir, sha256);

  return path;
}

blob_manifest *blob_manifest_new(void) {
  blob_manifest *m = malloc(sizeof(blob_manifest));

  if (!m) {
    return NULL;
  }

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
  if (!m) {
    return;
  }

  free(m->entries);
  free(m);
}

static bool blob_manifest_add(blob_manifest *m, const blob_entry *entry) {
  int new_cap;
  blob_entry *new_entries;

  if (m->count >= m->capacity) {
    new_cap = m->capacity * 2;
    new_entries = realloc(m->entries, new_cap * sizeof(blob_entry));

    if (!new_entries) {
      return false;
    }

    m->entries = new_entries;
    m->capacity = new_cap;
  }

  m->entries[m->count++] = *entry;

  return true;
}

static bool copy_file(const char *src, const char *dest) {
  char buf[65536];
  char tmp_path[1024];

  snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%ld", dest, (long)getpid());

  FILE *in = fopen(src, "rb");

  if (!in) {
    return false;
  }

  FILE *out = fopen(tmp_path, "wb");

  if (!out) {
    fclose(in);
    return false;
  }

  size_t n;

  while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
    if (fwrite(buf, 1, n, out) != n) {
      fclose(in);
      fclose(out);
      unlink(tmp_path);
      return false;
    }
  }

  fclose(in);
  fclose(out);

  if (rename(tmp_path, dest) != 0) {
    unlink(tmp_path);
    return false;
  }

  return true;
}

bool blob_store(dache *d, const char *path, blob_manifest *m) {
  if (!d || !path || !m) {
    return false;
  }

  struct stat st;

  if (stat(path, &st) != 0) {
    fprintf(stderr, "[dache] cannot stat: %s\n", path);
    return false;
  }

  if (!S_ISREG(st.st_mode)) {
    fprintf(stderr, "[dache] not a regular file: %s\n", path);
    return false;
  }

  uint8_t digest_buf[32];

  if (digest_from_file(path, digest_buf) != 0) {
    fprintf(stderr, "[dache] failed to hash: %s\n", path);
    return false;
  }

  blob_entry entry = {0};
  strncpy(entry.path, path, sizeof(entry.path) - 1);
  entry.path[sizeof(entry.path) - 1] = '\0';
  digest_to_hex(digest_buf, entry.sha256);
  entry.size = st.st_size;
  entry.mode = st.st_mode & 0777;

  char *blob_dir = get_blob_dir(d);

  if (!blob_dir) {
    return false;
  }

  if (!mkdir_p(blob_dir)) {
    fprintf(stderr, "[dache] cannot create blob dir: %s\n", blob_dir);
    free(blob_dir);
    return false;
  }

  free(blob_dir);

  char *blob_path = get_blob_path(d, entry.sha256);

  if (!blob_path) {
    return false;
  }

  if (access(blob_path, F_OK) != 0) {
    if (!copy_file(path, blob_path)) {
      fprintf(stderr, "[dache] failed to store blob: %s\n", path);
      free(blob_path);
      return false;
    }
  }

  if (hook_exists(d, "remote-put-blob")) {
    run_hook(d, "remote-put-blob", entry.sha256, blob_path);
  } else if (d->remote_dir) {
    char remote_blob[128];
    snprintf(remote_blob, sizeof(remote_blob), "blobs/%s", entry.sha256);
    remote_file_put(d, blob_path, remote_blob);
  }

  free(blob_path);
  return blob_manifest_add(m, &entry);
}

bool blob_restore(dache *d, const blob_entry *entry) {
  if (!d || !entry) {
    return false;
  }

  char *blob_dir = get_blob_dir(d);

  if (!blob_dir || !mkdir_p(blob_dir)) {
    fprintf(stderr, "[dache] cannot create blob dir\n");
    free(blob_dir);
    return false;
  }

  free(blob_dir);

  char *blob_path = get_blob_path(d, entry->sha256);

  if (!blob_path) {
    return false;
  }

  if (access(blob_path, F_OK) != 0) {
    bool fetched = false;

    if (hook_exists(d, "remote-get-blob")) {
      if (run_hook(d, "remote-get-blob", entry->sha256, blob_path) &&
          access(blob_path, F_OK) == 0) {
        fetched = true;
      }
    }

    if (!fetched && d->remote_dir) {
      char remote_blob[128];
      snprintf(remote_blob, sizeof(remote_blob), "blobs/%s", entry->sha256);
      if (remote_file_get(d, remote_blob, blob_path)) {
        fetched = true;
      }
    }

    if (!fetched) {
      fprintf(stderr, "[dache] blob not found: %s\n", entry->sha256);
      free(blob_path);
      return false;
    }

    uint8_t verify_digest[32];

    if (digest_from_file(blob_path, verify_digest) != 0) {
      fprintf(stderr, "[dache] failed to verify blob: %s\n", entry->sha256);
      unlink(blob_path);
      free(blob_path);
      return false;
    }

    char verify_hex[65];
    digest_to_hex(verify_digest, verify_hex);

    if (strcmp(verify_hex, entry->sha256) != 0) {
      fprintf(stderr, "[dache] blob integrity mismatch: expected %s, got %s\n",
              entry->sha256, verify_hex);
      unlink(blob_path);
      free(blob_path);
      return false;
    }
  }

  /* Ensure the destination directory exists. */
  char *path_copy = strdup(entry->path);

  if (path_copy) {
    char *last_slash = strrchr(path_copy, '/');

    if (last_slash) {
      *last_slash = '\0';
      mkdir_p(path_copy);
    }

    free(path_copy);
  }

  if (!copy_file(blob_path, entry->path)) {
    fprintf(stderr, "[dache] failed to restore: %s\n", entry->path);
    free(blob_path);
    return false;
  }

  chmod(entry->path, entry->mode);
  free(blob_path);

  return true;
}

static bool json_write_escaped(FILE *f, const char *s) {
  const unsigned char *p;

  if (fputc('"', f) == EOF) {
    return false;
  }

  for (p = (const unsigned char *)s; *p; p++) {
    switch (*p) {
    case '"':
      if (fputs("\\\"", f) == EOF) {
        return false;
      }

      break;
    case '\\':
      if (fputs("\\\\", f) == EOF) {
        return false;
      }

      break;
    case '\b':
      if (fputs("\\b", f) == EOF) {
        return false;
      }

      break;
    case '\f':
      if (fputs("\\f", f) == EOF) {
        return false;
      }

      break;
    case '\n':
      if (fputs("\\n", f) == EOF) {
        return false;
      }

      break;
    case '\r':
      if (fputs("\\r", f) == EOF) {
        return false;
      }

      break;
    case '\t':
      if (fputs("\\t", f) == EOF) {
        return false;
      }

      break;
    default:
      if (*p < 0x20) {
        if (fprintf(f, "\\u%04x", *p) < 0) {
          return false;
        }
      } else {
        if (fputc(*p, f) == EOF) {
          return false;
        }
      }

      break;
    }
  }

  if (fputc('"', f) == EOF) {
    return false;
  }

  return true;
}

bool blob_manifest_write(const blob_manifest *m, const char *path) {
  if (!m || !path) {
    return false;
  }

  /*
   * Write to <path>.tmp.<pid> then atomically rename. Mirrors copy_file +
   * write_archive: parallel snapshots against the same manifest can't
   * leave a half-written JSON behind.
   */
  char tmp_path[PATH_MAX];
  int n =
      snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%ld", path, (long)getpid());

  if (n < 0 || (size_t)n >= sizeof(tmp_path)) {
    fprintf(stderr, "[dache] manifest path too long: %s\n", path);
    return false;
  }

  FILE *f = fopen(tmp_path, "w");

  if (!f) {
    return false;
  }

  bool ok = true;

  if (fprintf(f, "{\n") < 0) {
    ok = false;
  }

  if (ok && fprintf(f, "  \"version\": %d,\n", MANIFEST_VERSION) < 0) {
    ok = false;
  }

  if (ok && fprintf(f, "  \"type\": \"individual\",\n") < 0) {
    ok = false;
  }

  if (ok && fprintf(f, "  \"files\": [\n") < 0) {
    ok = false;
  }

  for (int i = 0; ok && i < m->count; i++) {
    const blob_entry *e = &m->entries[i];

    if (fprintf(f, "    {\n      \"path\": ") < 0) {
      ok = false;
      break;
    }

    if (!json_write_escaped(f, e->path)) {
      ok = false;
      break;
    }

    if (fprintf(f, ",\n      \"sha256\": ") < 0) {
      ok = false;
      break;
    }

    if (!json_write_escaped(f, e->sha256)) {
      ok = false;
      break;
    }

    if (fprintf(f, ",\n      \"size\": %lu,\n", (unsigned long)e->size) < 0) {
      ok = false;
      break;
    }

    if (fprintf(f, "      \"mode\": %d\n", e->mode) < 0) {
      ok = false;
      break;
    }

    if (fprintf(f, "    }%s\n", (i < m->count - 1) ? "," : "") < 0) {
      ok = false;
      break;
    }
  }

  if (ok && fprintf(f, "  ]\n}\n") < 0) {
    ok = false;
  }

  if (fclose(f) != 0) {
    ok = false;
  }

  if (ok) {
    if (rename(tmp_path, path) != 0) {
      fprintf(stderr, "[dache] manifest rename '%s' -> '%s' failed: %s\n",
              tmp_path, path, strerror(errno));
      unlink(tmp_path);
      ok = false;
    }
  } else {
    unlink(tmp_path);
  }

  return ok;
}

static char *json_read_file(const char *path) {
  FILE *f = fopen(path, "r");

  if (!f) {
    return NULL;
  }

  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }

  long len = ftell(f);

  if (len < 0) {
    fclose(f);
    return NULL;
  }

  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return NULL;
  }

  char *buf = malloc((size_t)len + 1);

  if (!buf) {
    fclose(f);
    return NULL;
  }

  size_t n = fread(buf, 1, (size_t)len, f);

  if (n != (size_t)len) {
    free(buf);
    fclose(f);
    return NULL;
  }

  buf[len] = '\0';
  fclose(f);
  return buf;
}

static const char *json_skip_ws(const char *p) {
  while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
    p++;
  }

  return p;
}

static const char *json_parse_string(const char *p, char *out,
                                     size_t out_size) {
  char c;

  p = json_skip_ws(p);

  if (*p != '"') {
    return NULL;
  }

  p++;

  size_t i = 0;

  while (*p && *p != '"' && i < out_size - 1) {
    if (*p == '\\' && *(p + 1)) {
      p++;
      switch (*p) {
      case '"': c = '"'; break;
      case '\\': c = '\\'; break;
      case '/': c = '/'; break;
      case 'b': c = '\b'; break;
      case 'f': c = '\f'; break;
      case 'n': c = '\n'; break;
      case 'r': c = '\r'; break;
      case 't': c = '\t'; break;
      case 'u':
        /* Skip \uXXXX; we don't decode unicode. */
        if (p[1] && p[2] && p[3] && p[4]) {
          p += 4;
        }
        p++;
        continue;
      default: c = *p; break;
      }

      out[i++] = c;
      p++;
    } else {
      out[i++] = *p++;
    }
  }

  out[i] = '\0';

  if (*p == '"') {
    p++;
  }

  return p;
}

static const char *json_parse_number(const char *p, long *out) {
  char *end;

  p = json_skip_ws(p);
  *out = strtol(p, &end, 10);

  return end;
}

blob_manifest *blob_manifest_read(const char *path) {
  blob_entry entry;
  char key[64];
  long val;

  char *json = json_read_file(path);

  if (!json) {
    return NULL;
  }

  blob_manifest *m = blob_manifest_new();

  if (!m) {
    free(json);
    return NULL;
  }

  const char *p = json;
  const char *vp = strstr(p, "\"version\"");

  if (!vp) {
    fprintf(stderr, "[dache] manifest missing \"version\" field: %s\n", path);
    free(json);
    blob_manifest_free(m);
    return NULL;
  }

  vp = strchr(vp, ':');

  if (!vp) {
    fprintf(stderr, "[dache] manifest \"version\" field is malformed: %s\n",
            path);
    free(json);
    blob_manifest_free(m);
    return NULL;
  }

  long version;
  vp = json_parse_number(vp + 1, &version);

  if (version != MANIFEST_VERSION) {
    fprintf(stderr,
            "[dache] manifest version %ld is not supported "
            "(expected %d): %s\n",
            version, MANIFEST_VERSION, path);
    free(json);
    blob_manifest_free(m);
    return NULL;
  }

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

  while (*p) {
    p = json_skip_ws(p);

    if (*p == ']') {
      break;
    }

    if (*p == ',') {
      p++;
      continue;
    }

    if (*p != '{') {
      p++;
      continue;
    }

    p++;

    memset(&entry, 0, sizeof(entry));

    while (*p && *p != '}') {
      p = json_skip_ws(p);

      if (*p == '}') {
        break;
      }

      if (*p == ',') {
        p++;
        continue;
      }

      if (*p != '"') {
        p++;
        continue;
      }

      p = json_parse_string(p, key, sizeof(key));

      if (!p) {
        break;
      }

      p = json_skip_ws(p);

      if (*p == ':') {
        p++;
      }

      p = json_skip_ws(p);

      if (strcmp(key, "path") == 0) {
        p = json_parse_string(p, entry.path, sizeof(entry.path));
      } else if (strcmp(key, "sha256") == 0) {
        p = json_parse_string(p, entry.sha256, sizeof(entry.sha256));
      } else if (strcmp(key, "size") == 0) {
        p = json_parse_number(p, &val);
        entry.size = val;
      } else if (strcmp(key, "mode") == 0) {
        p = json_parse_number(p, &val);
        entry.mode = (int)val;
      }
    }

    if (*p == '}') {
      p++;
    }

    if (entry.path[0] && entry.sha256[0]) {
      blob_manifest_add(m, &entry);
    }
  }

  free(json);
  return m;
}
