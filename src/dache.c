#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <openssl/evp.h>

#include "dache.h"

#define DEFAULT_CACHE_DIR ".cache/dache"
#define DEFAULT_HOOKS_DIR ".config/dache/hooks"

static char *get_default_cache_dir(void) {
  const char *home;
  size_t len;
  char *path;

  home = getenv("HOME");
  if (!home) {
    return NULL;
  }

  len = strlen(home) + 1 + strlen(DEFAULT_CACHE_DIR) + 1;
  path = malloc(len);
  if (!path) {
    return NULL;
  }

  snprintf(path, len, "%s/%s", home, DEFAULT_CACHE_DIR);
  return path;
}

static char *get_default_hooks_dir(void) {
  const char *home;
  size_t len;
  char *path;

  home = getenv("HOME");
  if (!home) {
    return NULL;
  }

  len = strlen(home) + 1 + strlen(DEFAULT_HOOKS_DIR) + 1;
  path = malloc(len);
  if (!path) {
    return NULL;
  }

  snprintf(path, len, "%s/%s", home, DEFAULT_HOOKS_DIR);
  return path;
}

static bool ensure_dir_exists(const char *path) {
  struct stat st;
  if (stat(path, &st) == 0) {
    return S_ISDIR(st.st_mode);
  }
  /* Create directory (and parent if needed) */
  /* Simple approach: just try mkdir, assume parent exists */
  return mkdir(path, 0755) == 0;
}

dache *dache_new(const char *cache_dir, const char *remote_dir) {
  dache *d;
  char *parent;
  char *last_slash;

  d = malloc(sizeof(dache));
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

  if (!ensure_dir_exists(d->cache_dir)) {
    parent = strdup(d->cache_dir);
    last_slash = strrchr(parent, '/');

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
    free(d->remote_dir);
    free(d->hooks_dir);
    free(d);
  }
}

static bool copy_file(const char *src, const char *dest);

static char *get_hook_path(dache *d, const char *hook_name) {
  size_t len;
  char *path;

  if (!d->hooks_dir) {
    return NULL;
  }
  len = strlen(d->hooks_dir) + 1 + strlen(hook_name) + 1;
  path = malloc(len);
  if (!path) {
    return NULL;
  }
  snprintf(path, len, "%s/%s", d->hooks_dir, hook_name);
  return path;
}

static bool hook_exists(dache *d, const char *hook_name) {
  char *path;
  bool exists;

  path = get_hook_path(d, hook_name);
  if (!path) {
    return false;
  }
  exists = access(path, X_OK) == 0;
  free(path);
  return exists;
}

static bool run_hook(dache *d, const char *hook_name, const char *arg1,
                     const char *arg2) {
  char *hook_path;
  size_t cmd_len;
  char *cmd;
  int ret;

  hook_path = get_hook_path(d, hook_name);
  if (!hook_path) {
    return false;
  }

  if (access(hook_path, X_OK) != 0) {
    free(hook_path);
    return false;
  }

  /* Build command: hook_path arg1 arg2 */
  cmd_len = strlen(hook_path) + 1 + strlen(arg1) + 1 + strlen(arg2) + 1;
  cmd = malloc(cmd_len + 32); /* extra space for quotes */
  if (!cmd) {
    free(hook_path);
    return false;
  }

  snprintf(cmd, cmd_len + 32, "%s '%s' '%s'", hook_path, arg1, arg2);
  ret = system(cmd);

  free(cmd);
  free(hook_path);
  return ret == 0;
}

static bool remote_file_get(dache *d, const char *remote_path,
                            const char *local_path) {
  size_t len;
  char *full_remote;
  bool ok;

  if (!d->remote_dir) {
    return false;
  }

  len = strlen(d->remote_dir) + 1 + strlen(remote_path) + 1;
  full_remote = malloc(len);
  if (!full_remote) {
    return false;
  }
  snprintf(full_remote, len, "%s/%s", d->remote_dir, remote_path);

  if (access(full_remote, F_OK) != 0) {
    free(full_remote);
    return false;
  }

  ok = copy_file(full_remote, local_path);
  free(full_remote);
  return ok;
}

static bool remote_file_put(dache *d, const char *local_path,
                            const char *remote_path) {
  size_t len;
  char *full_remote;
  char *parent;
  char *last_slash;
  char *p;
  bool ok;

  if (!d->remote_dir) {
    return false;
  }

  len = strlen(d->remote_dir) + 1 + strlen(remote_path) + 1;
  full_remote = malloc(len);
  if (!full_remote) {
    return false;
  }
  snprintf(full_remote, len, "%s/%s", d->remote_dir, remote_path);

  parent = strdup(full_remote);
  last_slash = strrchr(parent, '/');
  if (last_slash) {
    *last_slash = '\0';
    p = parent;
    while (*p) {
      if (*p == '/' && p != parent) {
        *p = '\0';
        mkdir(parent, 0755);
        *p = '/';
      }
      p++;
    }
    mkdir(parent, 0755);
  }
  free(parent);

  ok = copy_file(local_path, full_remote);
  free(full_remote);
  return ok;
}

static int digest(EVP_MD_CTX *ctx, const char *path) {
  FILE *f;
  unsigned char buff[65536];
  size_t n;

  f = fopen(path, "rb");
  if (!f) {
    fprintf(stderr, "[dache] could not open file: %s\n", path);
    return -1;
  }

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
                    unsigned char out_digest[32]) {
  EVP_MD_CTX *ctx;
  const EVP_MD *md;
  int i, j, k, code;
  unsigned int len;

  ctx = EVP_MD_CTX_new();
  if (!ctx) {
    return -1;
  }

  md = EVP_sha256();
  if (!EVP_DigestInit_ex(ctx, md, NULL)) {
    EVP_MD_CTX_free(ctx);
    return -2;
  }

  if (!EVP_DigestUpdate(ctx, "env:\n", 5)) {
    EVP_MD_CTX_free(ctx);
    return -3;
  }

  for (i = 0; i < envc; i++) {
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

  for (j = 0; j < inputc; j++) {
    code = digest(ctx, inputv[j]);
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

  for (k = 0; k < commandc; k++) {
    if (!EVP_DigestUpdate(ctx, commandv[k], strlen(commandv[k]))) {
      EVP_MD_CTX_free(ctx);
      return -10;
    }
  }

  if (!EVP_DigestUpdate(ctx, "\n", 1)) {
    EVP_MD_CTX_free(ctx);
    return -11;
  }

  len = 0;
  if (!EVP_DigestFinal_ex(ctx, out_digest, &len)) {
    EVP_MD_CTX_free(ctx);
    return -12;
  }

  EVP_MD_CTX_free(ctx);
  return 0;
}

static char *cache_path_for_key(dache *d, const char *key) {
  /* <cache_dir>/<key>.tar.gz */
  size_t len;
  char *path;

  len = strlen(d->cache_dir) + 1 + 64 + 7 + 1; /* 64 hex + ".tar.gz" */
  path = malloc(len);
  if (!path) {
    return NULL;
  }
  snprintf(path, len, "%s/%s.tar.gz", d->cache_dir, key);
  return path;
}

bool dache_cache_get(dache *d, const char *key) {
  char *path;
  bool ok;
  char remote_key[128];

  if (!d || !key) {
    return false;
  }

  path = cache_path_for_key(d, key);
  if (!path) {
    return false;
  }

  if (access(path, F_OK) == 0) {
    ok = unarchive(path, ".");
    free(path);
    if (ok) {
      fprintf(stderr, "[dache] cache hit (local): %s\n", key);
    }
    return ok;
  }

  if (hook_exists(d, "remote-get")) {
    if (run_hook(d, "remote-get", key, path)) {
      if (access(path, F_OK) == 0) {
        ok = unarchive(path, ".");
        free(path);
        if (ok) {
          fprintf(stderr, "[dache] cache hit (hook): %s\n", key);
        }
        return ok;
      }
    }
  }

  snprintf(remote_key, sizeof(remote_key), "%s.tar.gz", key);

  if (remote_file_get(d, remote_key, path)) {
    ok = unarchive(path, ".");
    free(path);
    if (ok) {
      fprintf(stderr, "[dache] cache hit (remote): %s\n", key);
    }
    return ok;
  }

  free(path);
  return false;
}

bool dache_cache_put(dache *d, const char *key, const char **outputv,
                     int outputc) {
  char *path;
  const char **srcs;
  int i;
  bool ok;
  char remote_key[128];

  if (!d || !key || !outputv || outputc <= 0) {
    return false;
  }

  path = cache_path_for_key(d, key);
  if (!path) {
    return false;
  }

  srcs = malloc((outputc + 1) * sizeof(char *));
  if (!srcs) {
    free(path);
    return false;
  }

  for (i = 0; i < outputc; i++) {
    srcs[i] = outputv[i];
  }
  srcs[outputc] = NULL;

  ok = write_archive(srcs, path);
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
  DIR *dir;
  struct dirent *entry;
  size_t path_len, name_len, full_len;
  char *full_path;

  if (stat(path, &st) != 0) {
    fprintf(stderr, "[dache] warning: cannot stat '%s'\n", path);
    return false;
  }

  if (S_ISREG(st.st_mode)) {
    return expanded_paths_add(ep, path);
  }

  if (S_ISDIR(st.st_mode)) {
    dir = opendir(path);
    if (!dir) {
      fprintf(stderr, "[dache] warning: cannot open directory '%s'\n", path);
      return false;
    }

    while ((entry = readdir(dir)) != NULL) {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
        continue;
      }

      path_len = strlen(path);
      name_len = strlen(entry->d_name);
      full_len = path_len + 1 + name_len + 1;
      full_path = malloc(full_len);
      if (!full_path) {
        closedir(dir);
        return false;
      }

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

  return true;
}

static int path_compare(const void *a, const void *b) {
  return strcmp(*(const char **)a, *(const char **)b);
}

expanded_paths *expand_paths(const char **pathv, int pathc) {
  expanded_paths *ep;
  int i;

  ep = expanded_paths_new();
  if (!ep) {
    return NULL;
  }

  for (i = 0; i < pathc; i++) {
    expand_path_recursive(ep, pathv[i]);
  }

  if (ep->count > 1) {
    qsort(ep->paths, ep->count, sizeof(char *), path_compare);
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
  blob_manifest *m;

  m = malloc(sizeof(blob_manifest));
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
  FILE *in;
  FILE *out;
  char buf[65536];
  size_t n;

  in = fopen(src, "rb");
  if (!in) {
    return false;
  }

  out = fopen(dest, "wb");
  if (!out) {
    fclose(in);
    return false;
  }

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
  struct stat st;
  unsigned char digest_buf[32];
  blob_entry entry;
  char *blob_dir;
  char *blob_path;
  char remote_blob[128];

  if (!d || !path || !m) {
    return false;
  }

  if (stat(path, &st) != 0) {
    fprintf(stderr, "[dache] cannot stat: %s\n", path);
    return false;
  }

  if (!S_ISREG(st.st_mode)) {
    fprintf(stderr, "[dache] not a regular file: %s\n", path);
    return false;
  }

  if (digest_from_file(path, digest_buf) != 0) {
    fprintf(stderr, "[dache] failed to hash: %s\n", path);
    return false;
  }

  memset(&entry, 0, sizeof(entry));
  strncpy(entry.path, path, sizeof(entry.path) - 1);
  entry.path[sizeof(entry.path) - 1] = '\0';
  digest_to_hex(digest_buf, entry.sha256);
  entry.size = st.st_size;
  entry.mode = st.st_mode & 0777;

  blob_dir = get_blob_dir(d);
  if (!blob_dir) {
    return false;
  }
  ensure_dir_exists(blob_dir);
  free(blob_dir);

  blob_path = get_blob_path(d, entry.sha256);
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
    snprintf(remote_blob, sizeof(remote_blob), "blobs/%s", entry.sha256);
    remote_file_put(d, blob_path, remote_blob);
  }

  free(blob_path);

  return blob_manifest_add(m, &entry);
}

bool blob_restore(dache *d, const blob_entry *entry) {
  char *blob_path;
  bool fetched;
  char remote_blob[128];
  char *path_copy;
  char *last_slash;
  char *p;

  if (!d || !entry) {
    return false;
  }

  blob_path = get_blob_path(d, entry->sha256);
  if (!blob_path) {
    return false;
  }

  if (access(blob_path, F_OK) != 0) {
    fetched = false;

    if (hook_exists(d, "remote-get-blob")) {
      if (run_hook(d, "remote-get-blob", entry->sha256, blob_path)) {
        if (access(blob_path, F_OK) == 0) {
          fetched = true;
        }
      }
    }

    if (!fetched && d->remote_dir) {
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
  }

  path_copy = strdup(entry->path);
  if (path_copy) {
    last_slash = strrchr(path_copy, '/');
    if (last_slash) {
      *last_slash = '\0';
      p = path_copy;

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

  if (!copy_file(blob_path, entry->path)) {
    fprintf(stderr, "[dache] failed to restore: %s\n", entry->path);
    free(blob_path);
    return false;
  }

  chmod(entry->path, entry->mode);

  free(blob_path);
  return true;
}

bool blob_manifest_write(const blob_manifest *m, const char *path) {
  FILE *f;
  int i;
  const blob_entry *e;

  if (!m || !path) {
    return false;
  }

  f = fopen(path, "w");
  if (!f) {
    return false;
  }

  fprintf(f, "{\n");
  fprintf(f, "  \"version\": 1,\n");
  fprintf(f, "  \"type\": \"individual\",\n");
  fprintf(f, "  \"files\": [\n");

  for (i = 0; i < m->count; i++) {
    e = &m->entries[i];
    fprintf(f, "    {\n");
    fprintf(f, "      \"path\": \"%s\",\n", e->path);
    fprintf(f, "      \"sha256\": \"%s\",\n", e->sha256);
    fprintf(f, "      \"size\": %lu,\n", (unsigned long)e->size);
    fprintf(f, "      \"mode\": %d\n", e->mode);
    fprintf(f, "    }%s\n", (i < m->count - 1) ? "," : "");
  }

  fprintf(f, "  ]\n");
  fprintf(f, "}\n");

  fclose(f);
  return true;
}

static char *json_read_file(const char *path) {
  FILE *f;
  long len;
  char *buf;

  f = fopen(path, "r");
  if (!f) {
    return NULL;
  }

  fseek(f, 0, SEEK_END);
  len = ftell(f);
  fseek(f, 0, SEEK_SET);

  buf = malloc(len + 1);
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
  while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
    p++;
  }
  return p;
}

static const char *json_parse_string(const char *p, char *out,
                                     size_t out_size) {
  size_t i;

  p = json_skip_ws(p);
  if (*p != '"') {
    return NULL;
  }
  p++;

  i = 0;
  while (*p && *p != '"' && i < out_size - 1) {
    if (*p == '\\' && *(p + 1)) {
      p++;
    }
    out[i++] = *p++;
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
  char *json;
  blob_manifest *m;
  const char *p;
  blob_entry entry;
  char key[64];
  long val;

  json = json_read_file(path);
  if (!json) {
    return NULL;
  }

  m = blob_manifest_new();
  if (!m) {
    free(json);
    return NULL;
  }

  p = json;
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
