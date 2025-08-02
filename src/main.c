#include <getopt.h>
#include <openssl/sha.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

//#include "compression.h"
#include "dache.h"
#include "digest.h"

#define DACHE_VERSION "0.0.1"

struct dache_config {
  const char **inputsv;
  size_t inputc;
  const char **outputsv;
  size_t outputsc;
  const char **envv;
  size_t envc;
};

void show_help(void) {
  puts("Usage: dache [options] [target]");
  puts("Options:");
  puts("  -i INPUTS,  --inputs INPUTS     ");
  puts("  -o OUTPUTS, --outputs OUTPUTS   ");
  puts("  -e ENV,     --env ENV           ");
  puts("  -h, --help                  Shows this message");
  puts("  -v, --version               Print Dagwood version");
}

void print_version(void) {
  printf("%s\n", DACHE_VERSION);
}

int parse_args(int argc, const char **argv) {
  const char* describe  = NULL;
  const char* directory = NULL;
  const char* file      = NULL;
  const char* task_name = NULL;
  int list = 0;
  int repl = 0;
  int dot  = 0;

  struct option long_options[] = {
    {"directory", required_argument, 0, 'C'},
    {"list",      no_argument,       0, 'l'},
    {"describe",  required_argument, 0, 'd'},
    {"file",      required_argument, 0, 'f'},
    {"dot",       optional_argument, 0, 't'},
    {"repl",      no_argument,       0, 'r'},
    {"help",      no_argument,       0, 'h'},
    {"version",   no_argument,       0, 'v'},
    {0, 0, 0, 0}
  };

  int opt;

  while ((opt = getopt_long(argc, (char * const *) argv, "C:ldft:rh",
			    long_options, NULL)) != -1) {
    switch (opt) {
    case 'C': directory = optarg; break;
    case 'd': describe  = optarg; break;
    case 'f': file      = optarg; break;
    case 'l': list      = 1; break;
    case 'r': repl      = 1; break;
    case 'h':
      show_help();
      return EXIT_SUCCESS;
    case 't':
      dot  = 1;
      task_name = optarg;
      break;
    case 'v': print_version(); return EXIT_SUCCESS;
    default: show_help();      return EXIT_FAILURE;
    }
  }

  if (directory && chdir(directory) != 0) {
    perror("chdir failed");
    return EXIT_FAILURE;
  }

  if (!file) {
    file = "Dagwood";
  }

  if (repl) {
    return EXIT_FAILURE;
  }

  if (list) {
    return EXIT_FAILURE;
  }

  if (describe) {
    puts("[TODO] implement describe");
    return EXIT_FAILURE;
  }

  if (dot) {
    puts("[TODO] implement graphviz dump");
    return EXIT_FAILURE;
  }

  if (optind < argc) {
    return EXIT_FAILURE;
  }

  int result = EXIT_SUCCESS;

  return result;
}


int main(int argc, const char** argv) {
  uint8_t digest[32];
  char hex[65];

  const char* envv[2]     = {"PATH=/usr/bin/", "CC=gcc"};
  const char* inputv[2]   = {"src/main.c", "src/dache.h"};
  const char* commandv[4] = {"gcc", "test.c", "-o", "test"};
  
  int code = dache_cache_key(envv, 2,
			     inputv, 2,
			     commandv, 4,
			     digest);
  
  if (code != 0) {
    fprintf(stderr, "[dache] error code: %i\n", code);
  }

  digest_to_hex(digest, hex);

  printf("%s\n", hex);

  exit(0);
}
