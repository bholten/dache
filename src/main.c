#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "dache.h"

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
      "  dache snapshot [options] <files...>   Snapshot files for asset versioning");
  puts("  dache restore <manifest.json>         Restore files from snapshot");
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
  const char *output_file = NULL;
  const char *remote_dir = NULL;

  static struct option long_options[] = {
      {"output", required_argument, 0, 'o'},
      {"remote", required_argument, 0, 'r'},
      {"help",   no_argument,       0, 'h'},
      {0,        0,                 0, 0  }
  };

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

  const char **pathv = (const char **)&argv[optind];
  int pathc = argc - optind;
  expanded_paths *files = expand_paths(pathv, pathc);

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
  const char *remote_dir = NULL;

  static struct option long_options[] = {
      {"remote", required_argument, 0, 'r'},
      {"help",   no_argument,       0, 'h'},
      {0,        0,                 0, 0  }
  };

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

static int cmd_cache(int argc, char **argv) {
  struct dache_config cfg;
  config_init(&cfg);
  const char *remote_dir = NULL;

  static struct option long_options[] = {
      {"input",   required_argument, 0, 'i'},
      {"output",  required_argument, 0, 'o'},
      {"env",     required_argument, 0, 'e'},
      {"remote",  required_argument, 0, 'r'},
      {"help",    no_argument,       0, 'h'},
      {"version", no_argument,       0, 'v'},
      {0,         0,                 0, 0  }
  };

  optind = 1; // Reset getopt

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
    case 'r':
      remote_dir = optarg;
      break;
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
  char cache_key[65];

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
    return EXIT_SUCCESS; // Command succeeded, just nothing to cache
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
