#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "dache.h"

static int strptr_cmp(const void *a, const void *b) {
  return strcmp(*(const char **)a, *(const char **)b);
}

#define DACHE_VERSION "0.0.1"
#define MAX_ARGS 256

struct dache_config {
  const char **inputv;
  int inputc;
  const char **outputv;
  int outputc;
  const char **envv;
  int envc;
  const char **commandv;
  int commandc;
};

static void config_init(struct dache_config *cfg) {
  cfg->inputv = malloc(MAX_ARGS * sizeof(char *));
  cfg->inputc = 0;
  cfg->outputv = malloc(MAX_ARGS * sizeof(char *));
  cfg->outputc = 0;
  cfg->envv = malloc(MAX_ARGS * sizeof(char *));
  cfg->envc = 0;
  cfg->commandv = NULL;
  cfg->commandc = 0;
}

static void config_free(struct dache_config *cfg) {
  free(cfg->inputv);
  free(cfg->outputv);
  free(cfg->envv);
}

static void show_help(void) {
  puts("Usage:");
  puts(
      "  dache [options] -- <command...>       Run command with build caching");
  puts("  dache cache [options] -- <command...> Same as above (explicit form)");
  puts(
      "  dache key [options] -- <command...>   Print the cache key + components");
  puts(
      "  dache snapshot [options] <files...>   Snapshot files for asset versioning");
  puts("  dache restore <manifest.json>         Restore files from snapshot");
  puts(
      "  dache prune [options]                 Remove old cached build artifacts");
  puts("");
  puts("Build cache options:");
  puts("  -i, --input FILE    Add input file/directory (can be repeated)");
  puts("  -o, --output FILE   Add output file/directory (can be repeated)");
  puts(
      "  -e, --env VAR=VAL   Add environment variable to cache key (can be repeated)");
  puts("  -r, --remote PATH   Remote cache directory (file:// path)");
  puts("");
  puts("Snapshot options:");
  puts("  -o, --output FILE   Output manifest file (default: stdout)");
  puts("  -r, --remote PATH   Remote blob storage directory (file:// path)");
  puts("");
  puts("General options:");
  puts("  -h, --help          Show this message");
  puts("  -v, --version       Print version");
  puts("");
  puts("Examples:");
  puts("  dache -i src/ -o build/ -e CC=gcc -- make");
  puts("  dache -i src/ -o build/ -r /mnt/shared/cache -- make");
  puts("  dache snapshot -o assets.json assets/textures/ assets/models/");
  puts("  dache restore assets.json");
}

static void print_version(void) {
  printf("dache %s\n", DACHE_VERSION);
}

static int run_command(const char **argv) {
  pid_t pid = fork();

  if (pid < 0) {
    perror("[dache] fork");
    return -1;
  }

  if (pid == 0) {
    execvp(argv[0], (char *const *)argv);
    perror("[dache] execvp");
    _exit(127);
  }

  int status;

  if (waitpid(pid, &status, 0) < 0) {
    perror("[dache] waitpid");
    return -1;
  }

  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }

  return -1;
}

static int cmd_snapshot(int argc, char **argv) {
  static struct option long_options[] = {
      {"output", required_argument, 0, 'o'},
      {"remote", required_argument, 0, 'r'},
      {"help",   no_argument,       0, 'h'},
      {0,        0,                 0, 0  }
  };

  const char *output_file = NULL;
  const char *remote_dir = NULL;
  optind = 1;

  int opt;

  while ((opt = getopt_long(argc, argv, "+o:r:h", long_options, NULL)) != -1) {
    switch (opt) {
    case 'o': output_file = optarg; break;
    case 'r': remote_dir = optarg; break;
    case 'h':
      puts("Usage: dache snapshot [-o manifest.json] [-r remote] <files...>");
      puts("");
      puts("Options:");
      puts("  -o, --output FILE   Output manifest file (required)");
      puts("  -r, --remote PATH   Remote blob storage directory");
      puts("  -h, --help          Show this message");
      return EXIT_SUCCESS;
    default: return EXIT_FAILURE;
    }
  }

  if (!output_file) {
    fprintf(stderr, "[dache] error: --output is required for snapshot\n");
    return EXIT_FAILURE;
  }

  if (optind >= argc) {
    fprintf(stderr, "[dache] error: no files specified for snapshot\n");
    return EXIT_FAILURE;
  }

  dache *d = dache_new(NULL, remote_dir);

  if (!d) {
    fprintf(stderr, "[dache] error: failed to initialize cache\n");
    return EXIT_FAILURE;
  }

  expanded_paths *files =
      expand_paths((const char **)&argv[optind], argc - optind);

  if (!files || files->count == 0) {
    fprintf(stderr, "[dache] error: no files found to snapshot\n");

    if (files) {
      expanded_paths_free(files);
    }

    dache_free(d);
    return EXIT_FAILURE;
  }

  blob_manifest *manifest = blob_manifest_new();

  if (!manifest) {
    expanded_paths_free(files);
    dache_free(d);
    return EXIT_FAILURE;
  }

  int stored = 0;

  for (int i = 0; i < files->count; i++) {
    if (blob_store(d, files->paths[i], manifest)) {
      stored++;
    }
  }

  fprintf(stderr, "[dache] snapshot: %d files stored\n", stored);

  if (!blob_manifest_write(manifest, output_file)) {
    fprintf(stderr, "[dache] error: failed to write manifest\n");
    blob_manifest_free(manifest);
    expanded_paths_free(files);
    dache_free(d);
    return EXIT_FAILURE;
  }

  fprintf(stderr, "[dache] manifest written: %s\n", output_file);

  blob_manifest_free(manifest);
  expanded_paths_free(files);
  dache_free(d);
  return EXIT_SUCCESS;
}

static int cmd_restore(int argc, char **argv) {
  static struct option long_options[] = {
      {"remote", required_argument, 0, 'r'},
      {"help",   no_argument,       0, 'h'},
      {0,        0,                 0, 0  }
  };

  const char *remote_dir = NULL;
  optind = 1;

  int opt;

  while ((opt = getopt_long(argc, argv, "+r:h", long_options, NULL)) != -1) {
    switch (opt) {
    case 'r': remote_dir = optarg; break;
    case 'h':
      puts("Usage: dache restore [-r remote] <manifest.json>");
      puts("");
      puts("Options:");
      puts("  -r, --remote PATH   Remote blob storage directory");
      puts("  -h, --help          Show this message");
      return EXIT_SUCCESS;
    default: return EXIT_FAILURE;
    }
  }

  if (optind >= argc) {
    fprintf(stderr, "Usage: dache restore [-r remote] <manifest.json>\n");
    return EXIT_FAILURE;
  }

  const char *manifest_file = argv[optind];

  dache *d = dache_new(NULL, remote_dir);

  if (!d) {
    fprintf(stderr, "[dache] error: failed to initialize cache\n");
    return EXIT_FAILURE;
  }

  blob_manifest *manifest = blob_manifest_read(manifest_file);

  if (!manifest) {
    fprintf(stderr, "[dache] error: failed to read manifest: %s\n",
            manifest_file);
    dache_free(d);
    return EXIT_FAILURE;
  }

  int restored = 0;
  int total = manifest->count;

  for (int i = 0; i < manifest->count; i++) {
    if (blob_restore(d, &manifest->entries[i])) {
      restored++;
    }
  }

  fprintf(stderr, "[dache] restored: %d/%d files\n", restored, total);

  blob_manifest_free(manifest);
  dache_free(d);

  return (restored == total) ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int parse_duration(const char *s, long *out_seconds) {
  char *end;
  long n = strtol(s, &end, 10);

  if (end == s || n < 0) {
    return -1;
  }

  long mult;
  switch (*end) {
  case 'd': mult = 86400; break;
  case 'h': mult = 3600; break;
  case 'm': mult = 60; break;
  case 's': mult = 1; break;
  default: return -1;
  }

  if (*(end + 1) != '\0') {
    return -1;
  }

  *out_seconds = n * mult;
  return 0;
}

/*
 * Parse sizes like "10G", "500M", "1024K", "1024". Case-insensitive on
 * the suffix; powers of 1024.
 */
static int parse_size(const char *s, long long *out_bytes) {
  char *end;
  long long n = strtoll(s, &end, 10);

  if (end == s || n < 0) {
    return -1;
  }

  long long mult;
  switch (*end) {
  case 'K':
  case 'k': mult = 1024LL; break;
  case 'M':
  case 'm': mult = 1024LL * 1024; break;
  case 'G':
  case 'g': mult = 1024LL * 1024 * 1024; break;
  case '\0': *out_bytes = n; return 0;
  default: return -1;
  }

  if (*(end + 1) != '\0') {
    return -1;
  }

  *out_bytes = n * mult;
  return 0;
}

static void format_size(long long bytes, char *buf, size_t buf_size) {
  if (bytes >= 1024LL * 1024 * 1024) {
    snprintf(buf, buf_size, "%.1f GB", bytes / (double)(1024LL * 1024 * 1024));
  } else if (bytes >= 1024 * 1024) {
    snprintf(buf, buf_size, "%.1f MB", bytes / (double)(1024 * 1024));
  } else if (bytes >= 1024) {
    snprintf(buf, buf_size, "%.1f KB", bytes / 1024.0);
  } else {
    snprintf(buf, buf_size, "%lld B", bytes);
  }
}

static void format_age(long seconds, char *buf, size_t buf_size) {
  if (seconds >= 86400) {
    snprintf(buf, buf_size, "%ld days ago", seconds / 86400);
  } else if (seconds >= 3600) {
    snprintf(buf, buf_size, "%ld hours ago", seconds / 3600);
  } else if (seconds >= 60) {
    snprintf(buf, buf_size, "%ld minutes ago", seconds / 60);
  } else {
    snprintf(buf, buf_size, "%ld seconds ago", seconds);
  }
}

struct prune_entry {
  char *path;
  char name[80];
  time_t mtime;
  long long size;
};

static int prune_entry_cmp_mtime_desc(const void *a, const void *b) {
  const struct prune_entry *pa = a;
  const struct prune_entry *pb = b;

  if (pa->mtime > pb->mtime) {
    return -1;
  }

  if (pa->mtime < pb->mtime) {
    return 1;
  }

  return 0;
}

static int scan_build_cache(const char *cache_dir,
                            struct prune_entry **out_entries, int *out_count) {
  DIR *dir = opendir(cache_dir);

  if (!dir) {
    return -1;
  }

  struct prune_entry *entries = NULL;
  int count = 0;
  int capacity = 0;

  struct dirent *de;

  while ((de = readdir(dir)) != NULL) {
    size_t len = strlen(de->d_name);

    if (len <= 7 || strcmp(de->d_name + len - 7, ".tar.gz") != 0) {
      continue;
    }

    char path[PATH_MAX];

    if (snprintf(path, sizeof(path), "%s/%s", cache_dir, de->d_name) >=
        (int)sizeof(path)) {
      continue;
    }

    struct stat st;

    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
      continue;
    }

    if (count >= capacity) {
      int new_cap = capacity ? capacity * 2 : 32;
      struct prune_entry *resized =
          realloc(entries, (size_t)new_cap * sizeof(*entries));

      if (!resized) {
        for (int i = 0; i < count; i++) {
          free(entries[i].path);
        }

        free(entries);
        closedir(dir);
        return -1;
      }

      entries = resized;
      capacity = new_cap;
    }

    entries[count].path = strdup(path);
    size_t name_len = len - 7;

    if (name_len >= sizeof(entries[count].name)) {
      name_len = sizeof(entries[count].name) - 1;
    }

    memcpy(entries[count].name, de->d_name, name_len);
    entries[count].name[name_len] = '\0';
    entries[count].mtime = st.st_mtime;
    entries[count].size = (long long)st.st_size;
    count++;
  }

  closedir(dir);

  if (count > 1) {
    qsort(entries, (size_t)count, sizeof(*entries), prune_entry_cmp_mtime_desc);
  }

  *out_entries = entries;
  *out_count = count;
  return 0;
}

static int cmd_prune(int argc, char **argv) {
  static struct option long_options[] = {
      {"older-than", required_argument, 0, 't'},
      {"max-size",   required_argument, 0, 's'},
      {"dry-run",    no_argument,       0, 'n'},
      {"help",       no_argument,       0, 'h'},
      {0,            0,                 0, 0  }
  };

  long older_than_seconds = 0;
  long long max_size_bytes = 0;
  bool have_older_than = false;
  bool have_max_size = false;
  bool dry_run = false;

  optind = 1;
  int opt;

  while ((opt = getopt_long(argc, argv, "t:s:nh", long_options, NULL)) != -1) {
    switch (opt) {
    case 't':
      if (parse_duration(optarg, &older_than_seconds) != 0) {
        fprintf(stderr,
                "[dache] error: invalid --older-than value '%s' "
                "(use e.g. 30d, 24h, 60m, 5s)\n",
                optarg);
        return EXIT_FAILURE;
      }

      have_older_than = true;
      break;

    case 's':
      if (parse_size(optarg, &max_size_bytes) != 0) {
        fprintf(stderr,
                "[dache] error: invalid --max-size value '%s' "
                "(use e.g. 10G, 500M, 1024K)\n",
                optarg);
        return EXIT_FAILURE;
      }

      have_max_size = true;
      break;

    case 'n': dry_run = true; break;

    case 'h':
      puts("Usage: dache prune [options]");
      puts("");
      puts("Remove cached build artifacts. Either --older-than or");
      puts("--max-size (or both) is required — there is no implicit");
      puts("eviction policy. Pruning is local-only and does not touch");
      puts("blob storage (blobs back committed snapshot manifests).");
      puts("");
      puts("Options:");
      puts("  --older-than DURATION   Remove entries with mtime older than");
      puts("                          DURATION (e.g. 30d, 24h, 60m)");
      puts("  --max-size SIZE         Keep newest entries up to SIZE total");
      puts("                          (e.g. 10G, 500M)");
      puts("  --dry-run               Show what would be removed, don't");
      puts("                          actually delete");
      puts("  -h, --help              Show this message");
      return EXIT_SUCCESS;

    default: return EXIT_FAILURE;
    }
  }

  if (!have_older_than && !have_max_size) {
    fprintf(stderr,
            "[dache] error: at least one of --older-than / --max-size is "
            "required\n");
    return EXIT_FAILURE;
  }

  dache *d = dache_new(NULL, NULL);

  if (!d) {
    fprintf(stderr, "[dache] error: failed to initialize cache\n");
    return EXIT_FAILURE;
  }

  struct prune_entry *entries = NULL;
  int count = 0;

  if (scan_build_cache(d->cache_dir, &entries, &count) != 0) {
    fprintf(stderr, "[dache] error: cannot scan cache dir %s: %s\n",
            d->cache_dir, strerror(errno));
    dache_free(d);

    return EXIT_FAILURE;
  }

  if (count == 0) {
    fprintf(stderr, "[dache] prune: cache is empty (%s)\n", d->cache_dir);
    free(entries);
    dache_free(d);
    return EXIT_SUCCESS;
  }

  bool *prune = calloc((size_t)count, sizeof(bool));

  if (!prune) {
    for (int i = 0; i < count; i++) {
      free(entries[i].path);
    }

    free(entries);
    dache_free(d);

    return EXIT_FAILURE;
  }

  time_t now = time(NULL);

  if (have_older_than) {
    for (int i = 0; i < count; i++) {
      if (now - entries[i].mtime > older_than_seconds) {
        prune[i] = true;
      }
    }
  }

  if (have_max_size) {
    long long running = 0;
    for (int i = 0; i < count; i++) {
      if (running + entries[i].size > max_size_bytes) {
        prune[i] = true;
      } else {
        running += entries[i].size;
      }
    }
  }

  int removed_count = 0;
  long long removed_bytes = 0;
  int kept_count = 0;
  long long kept_bytes = 0;

  if (dry_run) {
    fprintf(stderr, "[dache] prune --dry-run (%s):\n", d->cache_dir);
  }

  for (int i = 0; i < count; i++) {
    if (prune[i]) {
      removed_count++;
      removed_bytes += entries[i].size;

      if (dry_run) {
        char size_buf[32];
        char age_buf[64];
        format_size(entries[i].size, size_buf, sizeof(size_buf));
        format_age(now - entries[i].mtime, age_buf, sizeof(age_buf));
        fprintf(stderr, "  would remove  %s  %8s  (%s)\n", entries[i].name,
                size_buf, age_buf);
      } else {
        if (unlink(entries[i].path) != 0) {
          fprintf(stderr, "  [dache] warning: failed to remove %s: %s\n",
                  entries[i].path, strerror(errno));
        }
      }
    } else {
      kept_count++;
      kept_bytes += entries[i].size;
    }
  }

  char removed_size_buf[32];
  char kept_size_buf[32];
  format_size(removed_bytes, removed_size_buf, sizeof(removed_size_buf));
  format_size(kept_bytes, kept_size_buf, sizeof(kept_size_buf));

  fprintf(stderr, "[dache] prune%s: %d entries (%s) %s, %d kept (%s)\n",
          dry_run ? " --dry-run" : "", removed_count, removed_size_buf,
          dry_run ? "would be removed" : "removed", kept_count, kept_size_buf);

  free(prune);
  for (int i = 0; i < count; i++) {
    free(entries[i].path);
  }
  free(entries);
  dache_free(d);
  return EXIT_SUCCESS;
}

static int cmd_key(int argc, char **argv) {
  /*
   * Read-only sibling of cmd_cache: same -i/-e/-o/-r/-- shape so users
   * can copy-paste an existing cache invocation, but instead of running
   * the command we print the cache key plus the framed components that
   * fed into it. -o and -r are accepted but not used (they don't affect
   * the key).
   */
  static struct option long_options[] = {
      {"input",  required_argument, 0, 'i'},
      {"output", required_argument, 0, 'o'},
      {"env",    required_argument, 0, 'e'},
      {"remote", required_argument, 0, 'r'},
      {"help",   no_argument,       0, 'h'},
      {0,        0,                 0, 0  }
  };

  struct dache_config cfg;
  config_init(&cfg);
  optind = 1;

  int opt;

  while ((opt = getopt_long(argc, argv, "+i:o:e:r:h", long_options, NULL)) !=
         -1) {
    switch (opt) {
    case 'i':
      if (cfg.inputc < MAX_ARGS) {
        cfg.inputv[cfg.inputc++] = optarg;
      }
      break;
    case 'o': break;
    case 'e':
      if (cfg.envc < MAX_ARGS) {
        cfg.envv[cfg.envc++] = optarg;
      }
      break;
    case 'r': break;
    case 'h':
      puts("Usage: dache key [options] -- <command...>");
      puts("");
      puts("Prints the cache key and the framed components that produced");
      puts("it. -o and -r are accepted so you can paste an existing dache");
      puts("cache invocation verbatim, but they don't affect the key.");
      puts("");
      puts("Options:");
      puts("  -i, --input FILE    Add input file/directory");
      puts("  -e, --env VAR=VAL   Add environment variable");
      puts("  -o, --output FILE   Accepted for argv compatibility (unused)");
      puts("  -r, --remote PATH   Accepted for argv compatibility (unused)");
      puts("  -h, --help          Show this message");
      config_free(&cfg);
      return EXIT_SUCCESS;
    default: config_free(&cfg); return EXIT_FAILURE;
    }
  }

  if (optind < argc) {
    cfg.commandv = (const char **)&argv[optind];
    cfg.commandc = argc - optind;
  }

  if (cfg.inputc == 0) {
    fprintf(stderr, "[dache] error: at least one --input is required\n");
    config_free(&cfg);
    return EXIT_FAILURE;
  }

  if (cfg.commandc == 0) {
    fprintf(stderr, "[dache] error: command is required after options\n");
    config_free(&cfg);
    return EXIT_FAILURE;
  }

  expanded_paths *inputs = expand_paths(cfg.inputv, cfg.inputc);

  if (!inputs || inputs->count == 0) {
    fprintf(stderr, "[dache] error: no input files found\n");

    if (inputs) {
      expanded_paths_free(inputs);
    }

    config_free(&cfg);
    return EXIT_FAILURE;
  }

  uint8_t digest[32];
  int code = dache_cache_key(cfg.envv, cfg.envc, (const char **)inputs->paths,
                             inputs->count, cfg.commandv, cfg.commandc, digest);
  if (code != 0) {
    fprintf(stderr, "[dache] error: failed to compute cache key (code %d)\n",
            code);
    expanded_paths_free(inputs);
    config_free(&cfg);

    return EXIT_FAILURE;
  }

  char cache_key[65];
  digest_to_hex(digest, cache_key);

  printf("key: %s\n\n", cache_key);

  const char **env_sorted = NULL;

  if (cfg.envc > 0) {
    env_sorted = malloc((size_t)cfg.envc * sizeof(char *));

    if (!env_sorted) {
      expanded_paths_free(inputs);
      config_free(&cfg);
      return EXIT_FAILURE;
    }

    for (int i = 0; i < cfg.envc; i++) {
      env_sorted[i] = cfg.envv[i];
    }

    qsort(env_sorted, (size_t)cfg.envc, sizeof(char *), strptr_cmp);
  }

  printf("env (%d):\n", cfg.envc);

  for (int i = 0; i < cfg.envc; i++) {
    printf("  %s\n", env_sorted[i]);
  }

  if (cfg.envc == 0) {
    printf("  (none)\n");
  }
  free(env_sorted);

  printf("\ninputs (%d):\n", inputs->count);

  for (int i = 0; i < inputs->count; i++) {
    uint8_t file_digest[32];
    char file_hex[65];

    if (digest_from_file(inputs->paths[i], file_digest) != 0) {
      printf("  ERROR-HASHING  %s\n", inputs->paths[i]);
      continue;
    }

    digest_to_hex(file_digest, file_hex);
    printf("  %s  %s\n", file_hex, inputs->paths[i]);
  }

  printf("\ncommand (%d):\n", cfg.commandc);

  for (int i = 0; i < cfg.commandc; i++) {
    printf("  %s\n", cfg.commandv[i]);
  }

  expanded_paths_free(inputs);
  config_free(&cfg);
  return EXIT_SUCCESS;
}

static int cmd_cache(int argc, char **argv) {
  static struct option long_options[] = {
      {"input",   required_argument, 0, 'i'},
      {"output",  required_argument, 0, 'o'},
      {"env",     required_argument, 0, 'e'},
      {"remote",  required_argument, 0, 'r'},
      {"help",    no_argument,       0, 'h'},
      {"version", no_argument,       0, 'v'},
      {0,         0,                 0, 0  }
  };

  struct dache_config cfg;
  config_init(&cfg);
  const char *remote_dir = NULL;
  optind = 1;

  int opt;

  while ((opt = getopt_long(argc, argv, "+i:o:e:r:hv", long_options, NULL)) !=
         -1) {
    switch (opt) {
    case 'i':
      if (cfg.inputc < MAX_ARGS) {
        cfg.inputv[cfg.inputc++] = optarg;
      }

      break;
    case 'o':
      if (cfg.outputc < MAX_ARGS) {
        cfg.outputv[cfg.outputc++] = optarg;
      }

      break;
    case 'e':
      if (cfg.envc < MAX_ARGS) {
        cfg.envv[cfg.envc++] = optarg;
      }

      break;
    case 'r': remote_dir = optarg; break;
    case 'h':
      show_help();
      config_free(&cfg);

      return EXIT_SUCCESS;
    case 'v':
      print_version();
      config_free(&cfg);

      return EXIT_SUCCESS;
    default:
      show_help();
      config_free(&cfg);

      return EXIT_FAILURE;
    }
  }

  if (optind < argc) {
    cfg.commandv = (const char **)&argv[optind];
    cfg.commandc = argc - optind;
  }

  if (cfg.inputc == 0) {
    fprintf(stderr, "[dache] error: at least one --input is required\n");
    config_free(&cfg);
    return EXIT_FAILURE;
  }

  if (cfg.outputc == 0) {
    fprintf(stderr, "[dache] error: at least one --output is required\n");
    config_free(&cfg);
    return EXIT_FAILURE;
  }

  if (cfg.commandc == 0) {
    fprintf(stderr, "[dache] error: command is required after options\n");
    config_free(&cfg);
    return EXIT_FAILURE;
  }

  dache *d = dache_new(NULL, remote_dir);

  if (!d) {
    fprintf(stderr, "[dache] error: failed to initialize cache\n");
    config_free(&cfg);
    return EXIT_FAILURE;
  }

  expanded_paths *inputs = expand_paths(cfg.inputv, cfg.inputc);

  if (!inputs) {
    fprintf(stderr, "[dache] error: failed to expand input paths\n");
    dache_free(d);
    config_free(&cfg);
    return EXIT_FAILURE;
  }

  if (inputs->count == 0) {
    fprintf(stderr, "[dache] error: no input files found\n");
    expanded_paths_free(inputs);
    dache_free(d);
    config_free(&cfg);
    return EXIT_FAILURE;
  }

  uint8_t digest[32];
  int code = dache_cache_key(cfg.envv, cfg.envc, (const char **)inputs->paths,
                             inputs->count, cfg.commandv, cfg.commandc, digest);
  if (code != 0) {
    fprintf(stderr, "[dache] error: failed to compute cache key (code %d)\n",
            code);
    expanded_paths_free(inputs);
    dache_free(d);
    config_free(&cfg);
    return EXIT_FAILURE;
  }

  char cache_key[65];
  digest_to_hex(digest, cache_key);

  if (dache_cache_get(d, cache_key)) {
    expanded_paths_free(inputs);
    dache_free(d);
    config_free(&cfg);
    return EXIT_SUCCESS;
  }

  fprintf(stderr, "[dache] cache miss, running command...\n");

  int result = run_command(cfg.commandv);

  if (result != 0) {
    fprintf(stderr, "[dache] command failed with exit code %d\n", result);
    expanded_paths_free(inputs);
    dache_free(d);
    config_free(&cfg);
    return result;
  }

  expanded_paths *outputs = expand_paths(cfg.outputv, cfg.outputc);

  if (!outputs || outputs->count == 0) {
    fprintf(stderr, "[dache] warning: no output files found to cache\n");

    if (outputs) {
      expanded_paths_free(outputs);
    }

    expanded_paths_free(inputs);
    dache_free(d);
    config_free(&cfg);

    return EXIT_SUCCESS;
  }

  dache_cache_put(d, cache_key, (const char **)outputs->paths, outputs->count);

  expanded_paths_free(outputs);
  expanded_paths_free(inputs);
  dache_free(d);
  config_free(&cfg);
  return EXIT_SUCCESS;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    show_help();
    return EXIT_FAILURE;
  }

  if (strcmp(argv[1], "snapshot") == 0) {
    return cmd_snapshot(argc - 1, argv + 1);
  }

  if (strcmp(argv[1], "restore") == 0) {
    return cmd_restore(argc - 1, argv + 1);
  }

  if (strcmp(argv[1], "cache") == 0) {
    return cmd_cache(argc - 1, argv + 1);
  }

  if (strcmp(argv[1], "key") == 0) {
    return cmd_key(argc - 1, argv + 1);
  }

  if (strcmp(argv[1], "prune") == 0) {
    return cmd_prune(argc - 1, argv + 1);
  }

  if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
    show_help();
    return EXIT_SUCCESS;
  }

  if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
    print_version();
    return EXIT_SUCCESS;
  }

  return cmd_cache(argc, argv);
}
