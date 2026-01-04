#include <errno.h>
#include <fcntl.h>
#include <openssl/evp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "dache.h"

int digest_from_file(const char *path, uint8_t out[32]) {
  FILE *f = fopen(path, "rb");

  if (!f) {
    return -1;
  }

  EVP_MD_CTX *ctx = EVP_MD_CTX_new();

  if (!ctx) {
    fclose(f);
    return -2;
  }

  const EVP_MD *md = EVP_sha256();

  if (!EVP_DigestInit_ex(ctx, md, NULL)) {
    EVP_MD_CTX_free(ctx);
    fclose(f);
    return -3;
  }

  uint8_t buff[65536];
  size_t n;

  while ((n = fread(buff, 1, sizeof(buff), f)) > 0) {
    if (!EVP_DigestUpdate(ctx, buff, n)) {
      EVP_MD_CTX_free(ctx);
      fclose(f);
      return -4;
    }
  }

  unsigned int len = 0;

  if (!EVP_DigestFinal_ex(ctx, out, &len)) {
    EVP_MD_CTX_free(ctx);
    fclose(f);
    return -5;
  }

  EVP_MD_CTX_free(ctx);
  fclose(f);
  return 0;
}

void digest_to_hex(const uint8_t digest[32], char hex_out[65]) {
  for (size_t i = 0; i < 32; ++i) {
    sprintf(&hex_out[i * 2], "%02x", digest[i]);
  }
  hex_out[64] = '\0';
}
