#define _POSIX_C_SOURCE 200809L

#include <archive.h>
#include <archive_entry.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <zlib.h>

#include "dache.h"

bool write_archive(const char **src, const char *dest) {
  struct archive *a = archive_write_new();

  if (!a) {
    return false;
  }

  if (archive_write_add_filter_gzip(a) != ARCHIVE_OK ||
      archive_write_set_format_pax_restricted(a) != ARCHIVE_OK) {
    fprintf(stderr, "[dache] archive setup failed: %s\n",
            archive_error_string(a));
    archive_write_free(a);
    return false;
  }

  if (archive_write_open_filename(a, dest) != ARCHIVE_OK) {
    fprintf(stderr, "[dache] archive open failed: %s\n",
            archive_error_string(a));
    archive_write_free(a);
    return false;
  }

  struct archive_entry *entry = archive_entry_new();

  if (!entry) {
    archive_write_close(a);
    archive_write_free(a);
    return false;
  }

  bool ok = true;

  for (; *src && ok; src++) {
    struct stat st;

    if (stat(*src, &st) != 0) {
      fprintf(stderr, "[dache] cannot stat '%s' for archive\n", *src);
      ok = false;
      break;
    }

    int fd = open(*src, O_RDONLY);

    if (fd < 0) {
      fprintf(stderr, "[dache] cannot open '%s' for archive\n", *src);
      ok = false;
      break;
    }

    archive_entry_set_pathname(entry, *src);
    archive_entry_set_size(entry, st.st_size);
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, st.st_mode & 0777);

    if (archive_write_header(a, entry) != ARCHIVE_OK) {
      fprintf(stderr, "[dache] archive header failed for '%s': %s\n", *src,
              archive_error_string(a));
      close(fd);
      ok = false;
      break;
    }

    uint8_t buff[8192];
    ssize_t len;

    while ((len = read(fd, buff, sizeof(buff))) > 0) {
      if (archive_write_data(a, buff, (size_t)len) < 0) {
        fprintf(stderr, "[dache] archive write failed for '%s': %s\n", *src,
                archive_error_string(a));
        ok = false;
        break;
      }
    }

    if (len < 0) {
      fprintf(stderr, "[dache] read failed for '%s'\n", *src);
      ok = false;
    }

    close(fd);
    archive_entry_clear(entry);
  }

  archive_entry_free(entry);

  if (archive_write_close(a) != ARCHIVE_OK) {
    fprintf(stderr, "[dache] archive close failed: %s\n",
            archive_error_string(a));
    ok = false;
  }

  archive_write_free(a);

  if (!ok) {
    unlink(dest);
  }

  return ok;
}

static int copy_data(struct archive *ar, struct archive *aw) {
  int r;
  const void *buff;
  size_t size;
  la_int64_t offset;

  for (;;) {
    r = archive_read_data_block(ar, &buff, &size, &offset);

    if (r == ARCHIVE_EOF) {
      return ARCHIVE_OK;
    }

    if (r != ARCHIVE_OK) {
      fprintf(stderr, "%s\n", archive_error_string(ar));
      return r;
    }

    r = archive_write_data_block(aw, buff, size, offset);

    if (r < ARCHIVE_OK) {
      fprintf(stderr, "%s\n", archive_error_string(aw));
      return r;
    }
  }
}

bool unarchive(const char *src, const char *dest) {
  /* TODO: support extracting to specific directory */
  (void)dest;

  int flags = ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM |
              ARCHIVE_EXTRACT_SECURE_NODOTDOT |
              ARCHIVE_EXTRACT_SECURE_SYMLINKS |
              ARCHIVE_EXTRACT_SECURE_NOABSOLUTEPATHS;

  struct archive *a = archive_read_new();
  struct archive *ext = archive_write_disk_new();

  if (!a || !ext) {
    if (a) {
      archive_read_free(a);
    }
    if (ext) {
      archive_write_free(ext);
    }

    return false;
  }

  archive_write_disk_set_options(ext, flags);
  archive_read_support_filter_gzip(a);
  archive_read_support_format_tar(a);

  if (src != NULL && strcmp(src, "-") == 0) {
    src = NULL;
  }

  bool ok = true;

  if (archive_read_open_filename(a, src, 10240) != ARCHIVE_OK) {
    fprintf(stderr, "%s\n", archive_error_string(a));
    ok = false;

    goto cleanup;
  }

  for (;;) {
    struct archive_entry *entry;
    int r = archive_read_next_header(a, &entry);

    if (r == ARCHIVE_EOF) {
      break;
    }

    if (r != ARCHIVE_OK) {
      fprintf(stderr, "%s\n", archive_error_string(a));
      ok = false;
      break;
    }

    if (archive_write_header(ext, entry) != ARCHIVE_OK) {
      fprintf(stderr, "%s\n", archive_error_string(ext));
      ok = false;
      break;
    }

    if (copy_data(a, ext) != ARCHIVE_OK) {
      ok = false;
      break;
    }
  }

  archive_read_close(a);
  archive_write_close(ext);

cleanup:
  archive_read_free(a);
  archive_write_free(ext);
  return ok;
}

bool compress_file(const char *src, const char *dest) {
  FILE *in;
  gzFile out;
  char buffer[65536];
  size_t bytes_read;

  in = fopen(src, "rb");

  if (!in) {
    perror("fopen src");
    return false;
  }

  out = gzopen(dest, "wb");

  if (!out) {
    perror("gzopen dest");
    fclose(in);
    return false;
  }

  while ((bytes_read = fread(buffer, 1, sizeof(buffer), in)) > 0) {
    if (gzwrite(out, buffer, (unsigned int)bytes_read) != (int)bytes_read) {
      perror("gzwrite");
      gzclose(out);
      fclose(in);
      return false;
    }
  }

  fclose(in);
  gzclose(out);
  return true;
}

bool decompress_file(const char *src, const char *dest) {
  gzFile in;
  FILE *out;
  char buffer[65536];
  int bytes_read;

  in = gzopen(src, "rb");

  if (!in) {
    perror("gzopen src");
    return false;
  }

  out = fopen(dest, "wb");

  if (!out) {
    perror("fopen src");
    gzclose(in);
    return false;
  }

  while ((bytes_read = gzread(in, buffer, sizeof(buffer))) > 0) {
    fwrite(buffer, 1, bytes_read, out);
  }

  gzclose(in);
  fclose(out);

  return true;
}
