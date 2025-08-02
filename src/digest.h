#ifndef HASH_H
#define HASH_H

#include <stdint.h>

int digest_from_file(const char* path, uint8_t digset_out[32]);
void digest_to_hex(const uint8_t digest[32], char hex_out[65]);

#endif
