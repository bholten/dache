/*
 * Unit tests for dache. Linked against src/{dache,archive,digest}.c
 * (no main.c). Run with `make test`.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/dache.h"

static int tests_total = 0;
static int tests_failed = 0;
static const char *current_test = NULL;

#define T_OK(cond)                                                            \
  do {                                                                        \
    tests_total++;                                                            \
    if (!(cond)) {                                                            \
      tests_failed++;                                                         \
      fprintf(stderr, "  FAIL [%s] %s:%d: %s\n", current_test, __FILE__,      \
              __LINE__, #cond);                                               \
    }                                                                         \
  } while (0)

#define T_EQ_INT(a, b)                                                        \
  do {                                                                        \
    long _a = (long)(a);                                                      \
    long _b = (long)(b);                                                      \
    tests_total++;                                                            \
    if (_a != _b) {                                                           \
      tests_failed++;                                                         \
      fprintf(stderr, "  FAIL [%s] %s:%d: %s (%ld) != %s (%ld)\n",            \
              current_test, __FILE__, __LINE__, #a, _a, #b, _b);              \
    }                                                                         \
  } while (0)

#define T_EQ_STR(a, b)                                                        \
  do {                                                                        \
    const char *_a = (a);                                                     \
    const char *_b = (b);                                                     \
    tests_total++;                                                            \
    if (strcmp(_a, _b) != 0) {                                                \
      tests_failed++;                                                         \
      fprintf(stderr, "  FAIL [%s] %s:%d: \"%s\" != \"%s\"\n", current_test,  \
              __FILE__, __LINE__, _a, _b);                                    \
    }                                                                         \
  } while (0)

#define T_NEQ_STR(a, b)                                                       \
  do {                                                                        \
    const char *_a = (a);                                                     \
    const char *_b = (b);                                                     \
    tests_total++;                                                            \
    if (strcmp(_a, _b) == 0) {                                                \
      tests_failed++;                                                         \
      fprintf(stderr, "  FAIL [%s] %s:%d: \"%s\" should differ from \"%s\"\n",\
              current_test, __FILE__, __LINE__, _a, _b);                      \
    }                                                                         \
  } while (0)

#define RUN(fn)                                                               \
  do {                                                                        \
    current_test = #fn;                                                       \
    printf("- %s\n", #fn);                                                    \
    fn();                                                                     \
  } while (0)

/* ---------- helpers ---------- */

static char g_tmpdir[256];

static void make_tmpdir(void) {
  char *tmp;

  tmp = getenv("TMPDIR");
  if (!tmp) tmp = "/tmp";
  snprintf(g_tmpdir, sizeof(g_tmpdir), "%s/dache-test-XXXXXX", tmp);

  if (!mkdtemp(g_tmpdir)) {
    fprintf(stderr, "mkdtemp failed: %s\n", strerror(errno));
    exit(2);
  }
}

static int rm_rf(const char *path) {
  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "rm -rf '%s'", path);
  return system(cmd);
}

static void cleanup_tmpdir(void) {
  if (g_tmpdir[0]) rm_rf(g_tmpdir);
}

static char *tmp_path(const char *suffix) {
  size_t len = strlen(g_tmpdir) + 1 + strlen(suffix) + 1;
  char *p = malloc(len);
  snprintf(p, len, "%s/%s", g_tmpdir, suffix);
  return p;
}

static void write_file(const char *path, const char *content) {
  FILE *f = fopen(path, "wb");
  if (!f) {
    fprintf(stderr, "cannot create %s: %s\n", path, strerror(errno));
    exit(2);
  }
  fwrite(content, 1, strlen(content), f);
  fclose(f);
}

static void write_file_bytes(const char *path, const void *data, size_t n) {
  FILE *f = fopen(path, "wb");
  if (!f) exit(2);
  fwrite(data, 1, n, f);
  fclose(f);
}

static int file_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0;
}

/* ---------- digest tests ---------- */

static void test_digest_empty_file(void) {
  /* SHA-256 of empty string */
  static const char *expected =
      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
  char *path = tmp_path("empty");
  unsigned char digest[32];
  char hex[65];

  write_file(path, "");
  T_EQ_INT(digest_from_file(path, digest), 0);
  digest_to_hex(digest, hex);
  T_EQ_STR(hex, expected);

  unlink(path);
  free(path);
}

static void test_digest_known_content(void) {
  /* SHA-256 of "abc" */
  static const char *expected =
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
  char *path = tmp_path("abc");
  unsigned char digest[32];
  char hex[65];

  write_file(path, "abc");
  T_EQ_INT(digest_from_file(path, digest), 0);
  digest_to_hex(digest, hex);
  T_EQ_STR(hex, expected);

  unlink(path);
  free(path);
}

static void test_digest_missing_file(void) {
  unsigned char digest[32];
  T_OK(digest_from_file("/nonexistent/path/zzz", digest) != 0);
}

static void test_digest_to_hex_zeros(void) {
  unsigned char zeros[32];
  char hex[65];
  memset(zeros, 0, sizeof(zeros));
  digest_to_hex(zeros, hex);
  T_EQ_STR(hex, "0000000000000000000000000000000000000000000000000000000000000000");
}

/* ---------- cache key tests ---------- */

static void compute_key(const char **env, int envc, const char **inputs,
                        int inputc, const char **cmd, int cmdc, char *out_hex) {
  unsigned char digest[32];
  int code = dache_cache_key(env, envc, inputs, inputc, cmd, cmdc, digest);
  if (code != 0) {
    snprintf(out_hex, 65, "ERR%d", code);
    return;
  }
  digest_to_hex(digest, out_hex);
}

static void test_key_stable_across_runs(void) {
  char *a_path = tmp_path("in_a");
  const char *inputs[1];
  const char *cmd[2];
  char k1[65], k2[65];

  write_file(a_path, "hello");
  inputs[0] = a_path;
  cmd[0] = "make";
  cmd[1] = "all";

  compute_key(NULL, 0, inputs, 1, cmd, 2, k1);
  compute_key(NULL, 0, inputs, 1, cmd, 2, k2);
  T_EQ_STR(k1, k2);

  unlink(a_path);
  free(a_path);
}

static void test_key_env_order_independent(void) {
  char *path = tmp_path("in_env");
  const char *inputs[1];
  const char *cmd[1];
  const char *env_ab[2];
  const char *env_ba[2];
  char k_ab[65], k_ba[65];

  write_file(path, "x");
  inputs[0] = path;
  cmd[0] = "make";
  env_ab[0] = "A=1"; env_ab[1] = "B=2";
  env_ba[0] = "B=2"; env_ba[1] = "A=1";

  compute_key(env_ab, 2, inputs, 1, cmd, 1, k_ab);
  compute_key(env_ba, 2, inputs, 1, cmd, 1, k_ba);
  T_EQ_STR(k_ab, k_ba);

  unlink(path);
  free(path);
}

static void test_key_env_value_matters(void) {
  char *path = tmp_path("in_env2");
  const char *inputs[1];
  const char *cmd[1];
  const char *env_1[1];
  const char *env_2[1];
  char k1[65], k2[65];

  write_file(path, "x");
  inputs[0] = path;
  cmd[0] = "make";
  env_1[0] = "A=1";
  env_2[0] = "A=2";

  compute_key(env_1, 1, inputs, 1, cmd, 1, k1);
  compute_key(env_2, 1, inputs, 1, cmd, 1, k2);
  T_NEQ_STR(k1, k2);

  unlink(path);
  free(path);
}

static void test_key_input_content_matters(void) {
  char *path = tmp_path("in_content");
  const char *inputs[1];
  const char *cmd[1];
  char k1[65], k2[65];

  inputs[0] = path;
  cmd[0] = "make";

  write_file(path, "alpha");
  compute_key(NULL, 0, inputs, 1, cmd, 1, k1);

  write_file(path, "beta");
  compute_key(NULL, 0, inputs, 1, cmd, 1, k2);

  T_NEQ_STR(k1, k2);
  unlink(path);
  free(path);
}

static void test_key_input_path_matters(void) {
  /*
   * Regression: previously paths were ignored, so renaming a file
   * with the same content produced the same key.
   */
  char *path_a = tmp_path("rename_a");
  char *path_b = tmp_path("rename_b");
  const char *inputs_a[1];
  const char *inputs_b[1];
  const char *cmd[1];
  char k_a[65], k_b[65];

  write_file(path_a, "same");
  write_file(path_b, "same");

  inputs_a[0] = path_a;
  inputs_b[0] = path_b;
  cmd[0] = "make";

  compute_key(NULL, 0, inputs_a, 1, cmd, 1, k_a);
  compute_key(NULL, 0, inputs_b, 1, cmd, 1, k_b);

  T_NEQ_STR(k_a, k_b);

  unlink(path_a); unlink(path_b);
  free(path_a); free(path_b);
}

static void test_key_command_separated(void) {
  /*
   * Regression: previously command args were concatenated without
   * separator, so ["ab", "cd"] hashed the same as ["abcd"].
   */
  char *path = tmp_path("cmd_sep");
  const char *inputs[1];
  const char *cmd_two[2];
  const char *cmd_one[1];
  char k1[65], k2[65];

  write_file(path, "x");
  inputs[0] = path;
  cmd_two[0] = "ab"; cmd_two[1] = "cd";
  cmd_one[0] = "abcd";

  compute_key(NULL, 0, inputs, 1, cmd_two, 2, k1);
  compute_key(NULL, 0, inputs, 1, cmd_one, 1, k2);
  T_NEQ_STR(k1, k2);

  unlink(path);
  free(path);
}

static void test_key_command_order_matters(void) {
  char *path = tmp_path("cmd_order");
  const char *inputs[1];
  const char *cmd_ab[2];
  const char *cmd_ba[2];
  char k1[65], k2[65];

  write_file(path, "x");
  inputs[0] = path;
  cmd_ab[0] = "a"; cmd_ab[1] = "b";
  cmd_ba[0] = "b"; cmd_ba[1] = "a";

  compute_key(NULL, 0, inputs, 1, cmd_ab, 2, k1);
  compute_key(NULL, 0, inputs, 1, cmd_ba, 2, k2);
  T_NEQ_STR(k1, k2);

  unlink(path);
  free(path);
}

static void test_key_binary_input(void) {
  /* Verify the digest covers exact file length, not just NUL-terminated. */
  char *path = tmp_path("bin_input");
  const char *inputs[1];
  const char *cmd[1];
  char k1[65], k2[65];
  unsigned char a[8];
  unsigned char b[8];
  int i;

  for (i = 0; i < 8; i++) { a[i] = (unsigned char)i; b[i] = (unsigned char)(i + 1); }

  inputs[0] = path;
  cmd[0] = "make";

  write_file_bytes(path, a, sizeof(a));
  compute_key(NULL, 0, inputs, 1, cmd, 1, k1);

  write_file_bytes(path, b, sizeof(b));
  compute_key(NULL, 0, inputs, 1, cmd, 1, k2);

  T_NEQ_STR(k1, k2);
  unlink(path);
  free(path);
}

/* ---------- manifest tests ---------- */

static void test_manifest_roundtrip(void) {
  blob_manifest *out;
  blob_manifest *in;
  blob_entry e1, e2;
  char *path = tmp_path("manifest.json");

  out = blob_manifest_new();
  T_OK(out != NULL);

  memset(&e1, 0, sizeof(e1));
  strcpy(e1.path, "assets/textures/foo.png");
  strcpy(e1.sha256,
         "0000000000000000000000000000000000000000000000000000000000000001");
  e1.size = 1234;
  e1.mode = 0644;

  memset(&e2, 0, sizeof(e2));
  strcpy(e2.path, "assets/models/bar.obj");
  strcpy(e2.sha256,
         "0000000000000000000000000000000000000000000000000000000000000002");
  e2.size = 5678;
  e2.mode = 0755;

  /* Use the public API: blob_store would also work but needs filesystem.
   * For roundtrip we go via the in-memory manifest functions, so we manually
   * append via the only public path: re-walk through blob_store would require
   * actual files. Since blob_manifest_add is static, we instead test via
   * the e2e path. For unit test we use a workaround: write a JSON file
   * directly and read it back. */
  (void)e1; (void)e2; (void)out;
  blob_manifest_free(out);

  /* Write a manifest manually and read it back */
  {
    FILE *f = fopen(path, "w");
    fprintf(f, "{\n  \"version\": 1,\n  \"type\": \"individual\",\n  \"files\": [\n");
    fprintf(f, "    {\"path\": \"a/b.txt\", \"sha256\": \"deadbeef00000000000000000000000000000000000000000000000000000000\", \"size\": 42, \"mode\": 420},\n");
    fprintf(f, "    {\"path\": \"x/y.bin\", \"sha256\": \"cafebabe00000000000000000000000000000000000000000000000000000000\", \"size\": 99, \"mode\": 493}\n");
    fprintf(f, "  ]\n}\n");
    fclose(f);
  }

  in = blob_manifest_read(path);
  T_OK(in != NULL);
  if (in) {
    T_EQ_INT(in->count, 2);
    T_EQ_STR(in->entries[0].path, "a/b.txt");
    T_EQ_STR(in->entries[0].sha256,
             "deadbeef00000000000000000000000000000000000000000000000000000000");
    T_EQ_INT(in->entries[0].size, 42);
    T_EQ_INT(in->entries[0].mode, 420);
    T_EQ_STR(in->entries[1].path, "x/y.bin");
    T_EQ_INT(in->entries[1].size, 99);
    T_EQ_INT(in->entries[1].mode, 493);
    blob_manifest_free(in);
  }

  unlink(path);
  free(path);
}

static void test_manifest_write_then_read(void) {
  /* Drive write through blob_store so we exercise the real writer. */
  dache *d;
  blob_manifest *m;
  blob_manifest *roundtrip;
  char *cache = tmp_path("cache");
  char *src = tmp_path("payload.txt");
  char *manifest_path = tmp_path("snap.json");

  write_file(src, "hello blob world");

  d = dache_new(cache, NULL);
  T_OK(d != NULL);

  m = blob_manifest_new();
  T_OK(m != NULL);

  T_OK(blob_store(d, src, m));
  T_EQ_INT(m->count, 1);

  T_OK(blob_manifest_write(m, manifest_path));
  T_OK(file_exists(manifest_path));

  roundtrip = blob_manifest_read(manifest_path);
  T_OK(roundtrip != NULL);
  if (roundtrip) {
    T_EQ_INT(roundtrip->count, 1);
    T_EQ_STR(roundtrip->entries[0].path, src);
    T_EQ_STR(roundtrip->entries[0].sha256, m->entries[0].sha256);
    T_EQ_INT(roundtrip->entries[0].size, 16);
    blob_manifest_free(roundtrip);
  }

  blob_manifest_free(m);
  dache_free(d);
  unlink(src);
  unlink(manifest_path);
  rm_rf(cache);
  free(src);
  free(manifest_path);
  free(cache);
}

static void test_manifest_escapes_special_chars(void) {
  /*
   * Regression: paths with " or \ used to break the JSON output, making
   * roundtrip fail.
   */
  dache *d;
  blob_manifest *m;
  blob_manifest *roundtrip;
  char *cache = tmp_path("cache2");
  /* A path that contains a quote and a backslash. POSIX allows these. */
  char *src = tmp_path("weird\"name\\file");
  char *manifest_path = tmp_path("snap2.json");

  write_file(src, "payload");

  d = dache_new(cache, NULL);
  m = blob_manifest_new();

  T_OK(blob_store(d, src, m));
  T_OK(blob_manifest_write(m, manifest_path));

  roundtrip = blob_manifest_read(manifest_path);
  T_OK(roundtrip != NULL);
  if (roundtrip) {
    T_EQ_INT(roundtrip->count, 1);
    T_EQ_STR(roundtrip->entries[0].path, src);
    blob_manifest_free(roundtrip);
  }

  blob_manifest_free(m);
  dache_free(d);
  unlink(src);
  unlink(manifest_path);
  rm_rf(cache);
  free(src);
  free(manifest_path);
  free(cache);
}

/* ---------- expand_paths tests ---------- */

static void test_expand_paths_sorts(void) {
  char *dir = tmp_path("expand_dir");
  char *a;
  char *b;
  char *c;
  const char *roots[1];
  expanded_paths *ep;

  mkdir(dir, 0755);
  a = malloc(strlen(dir) + 16);
  b = malloc(strlen(dir) + 16);
  c = malloc(strlen(dir) + 16);
  sprintf(a, "%s/c.txt", dir);
  sprintf(b, "%s/a.txt", dir);
  sprintf(c, "%s/b.txt", dir);
  write_file(a, "1");
  write_file(b, "2");
  write_file(c, "3");

  roots[0] = dir;
  ep = expand_paths(roots, 1);
  T_OK(ep != NULL);
  if (ep) {
    T_EQ_INT(ep->count, 3);
    /* Sorted alphabetically: a, b, c */
    T_OK(strstr(ep->paths[0], "/a.txt") != NULL);
    T_OK(strstr(ep->paths[1], "/b.txt") != NULL);
    T_OK(strstr(ep->paths[2], "/c.txt") != NULL);
    expanded_paths_free(ep);
  }

  unlink(a); unlink(b); unlink(c);
  rmdir(dir);
  free(a); free(b); free(c); free(dir);
}

static void test_expand_paths_skips_symlink_loop(void) {
  /*
   * Regression: previously stat() followed symlinks, so a cycle would
   * infinite-loop.
   */
  char *dir = tmp_path("loop_dir");
  char *child;
  char *link;
  const char *roots[1];
  expanded_paths *ep;

  mkdir(dir, 0755);
  child = malloc(strlen(dir) + 16);
  sprintf(child, "%s/file.txt", dir);
  write_file(child, "ok");

  link = malloc(strlen(dir) + 16);
  sprintf(link, "%s/loop", dir);
  /* Symlink pointing back to parent dir; would cycle if followed. */
  symlink(dir, link);

  roots[0] = dir;
  ep = expand_paths(roots, 1);
  T_OK(ep != NULL);
  if (ep) {
    /* Must terminate. Should include file.txt; symlink must be skipped. */
    int saw_file = 0;
    int i;
    for (i = 0; i < ep->count; i++) {
      if (strstr(ep->paths[i], "file.txt")) saw_file = 1;
    }
    T_OK(saw_file);
    expanded_paths_free(ep);
  }

  unlink(child);
  unlink(link);
  rmdir(dir);
  free(child); free(link); free(dir);
}

/* ---------- archive tests ---------- */

static void test_archive_roundtrip(void) {
  /*
   * Mirrors real usage: dache invokes write_archive with the same
   * relative output paths it later extracts into the cwd.
   */
  char *work = tmp_path("arch_work");
  char *cwd_before;
  char *archive;
  const char *srcs[3];

  mkdir(work, 0755);
  cwd_before = getcwd(NULL, 0);
  T_EQ_INT(chdir(work), 0);

  mkdir("build", 0755);
  write_file("build/a.txt", "alpha");
  write_file("build/b.txt", "bravo");

  archive = tmp_path("out.tar.gz");
  srcs[0] = "build/a.txt";
  srcs[1] = "build/b.txt";
  srcs[2] = NULL;

  T_OK(write_archive(srcs, archive));
  T_OK(file_exists(archive));

  /* Wipe and extract back. */
  unlink("build/a.txt");
  unlink("build/b.txt");

  T_OK(unarchive(archive, "."));
  T_OK(file_exists("build/a.txt"));
  T_OK(file_exists("build/b.txt"));

  if (cwd_before) {
    chdir(cwd_before);
    free(cwd_before);
  }

  unlink(archive);
  rm_rf(work);
  free(archive);
  free(work);
}

static void test_archive_missing_file_fails(void) {
  /*
   * Regression: write_archive used to return true even when stat() of the
   * source failed.
   */
  char *archive = tmp_path("bad.tar.gz");
  const char *srcs[2];

  srcs[0] = "/definitely/does/not/exist/zzz";
  srcs[1] = NULL;

  T_OK(!write_archive(srcs, archive));
  T_OK(!file_exists(archive));

  free(archive);
}

/* ---------- runner ---------- */

int main(void) {
  make_tmpdir();
  printf("dache unit tests (tmpdir=%s)\n", g_tmpdir);

  RUN(test_digest_empty_file);
  RUN(test_digest_known_content);
  RUN(test_digest_missing_file);
  RUN(test_digest_to_hex_zeros);

  RUN(test_key_stable_across_runs);
  RUN(test_key_env_order_independent);
  RUN(test_key_env_value_matters);
  RUN(test_key_input_content_matters);
  RUN(test_key_input_path_matters);
  RUN(test_key_command_separated);
  RUN(test_key_command_order_matters);
  RUN(test_key_binary_input);

  RUN(test_manifest_roundtrip);
  RUN(test_manifest_write_then_read);
  RUN(test_manifest_escapes_special_chars);

  RUN(test_expand_paths_sorts);
  RUN(test_expand_paths_skips_symlink_loop);

  RUN(test_archive_roundtrip);
  RUN(test_archive_missing_file_fails);

  cleanup_tmpdir();

  printf("\n%d/%d passed", tests_total - tests_failed, tests_total);
  if (tests_failed) {
    printf(" (%d FAILED)\n", tests_failed);
    return 1;
  }
  printf("\n");
  return 0;
}
