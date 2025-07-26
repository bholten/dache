#ifndef COMPRESSION_H
#define COMPRESSION_H

#include <stdbool.h>

bool write_archive(const char** src, const char* dest);
bool unarchive(const char* src, const char* dest);
bool compress_file(const char* src, const char* dest);

#endif
