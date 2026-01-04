#include <errno.h>
#include <fcntl.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "dache.h"

int digest_from_file(const char *path, unsigned char out[32]) {
  FILE *f;
  EVP_MD_CTX *ctx;
  const EVP_MD *md;
  unsigned char buff[65536];
  size_t n;
  unsigned int len;

  f = fopen(path, "rb");
  if (!f) {
    return -1;
  }

  ctx = EVP_MD_CTX_new();
  if (!ctx) {
    fclose(f);
    return -2;
  }

  md = EVP_sha256();
  if (!EVP_DigestInit_ex(ctx, md, NULL)) {
    EVP_MD_CTX_free(ctx);
    fclose(f);
    return -3;
  }

  while ((n = fread(buff, 1, sizeof(buff), f)) > 0) {
    if (!EVP_DigestUpdate(ctx, buff, n)) {
      EVP_MD_CTX_free(ctx);
      fclose(f);
      return -4;
    }
  }

  len = 0;
  if (!EVP_DigestFinal_ex(ctx, out, &len)) {
    EVP_MD_CTX_free(ctx);
    fclose(f);
    return -5;
  }

  EVP_MD_CTX_free(ctx);
  fclose(f);
  return 0;
}

void digest_to_hex(const unsigned char digest[32], char hex_out[65]) {
  size_t i;

  for (i = 0; i < 32; ++i) {
    sprintf(&hex_out[i * 2], "%02x", digest[i]);
  }
  hex_out[64] = '\0';
}
