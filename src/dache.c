#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <openssl/evp.h>

#include "dache.h"

static int digest(EVP_MD_CTX *ctx, const EVP_MD *md, const char *path,
                  uint8_t out[32]) {
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

  uint8_t buff[65536];

  if (!EVP_DigestUpdate(ctx, "env:\n", 5)) {
    EVP_MD_CTX_free(ctx);
    return -3;
  }

  for (size_t i = 0; i < envc; i++) {
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

  for (size_t j = 0; j < inputc; j++) {
    uint8_t file_digest[32];
    int code = digest(ctx, md, inputv[j], file_digest);

    if (code != 0) {
      fprintf(stderr, "failed %s\n", inputv[j]);
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

  for (size_t k = 0; k < commandc; k++) {
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

bool dache_cache_get(dache *d, char key[64]) {
  return false;
}
