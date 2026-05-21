#define _POSIX_C_SOURCE 200809L

#include <archive.h>
#include <archive_entry.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <zlib.h>

#include "dache.h"

/*
 * Sentinel pathname written as the FIRST entry in every
 * dache-produced archive. The version number lives in the name so
 * that a future v2 format can be rejected cleanly by v1 readers (and
 * vice versa) without having to read the entry's body.
 *
 * On extract we recognize this entry, skip its (zero-byte) data, and
 * do NOT write it to disk — otherwise users would get a stray
 * __dache_format_v1 file in their working directory after every cache
 * hit.
 */
#define ARCHIVE_VERSION_MARKER "__dache_format_v1"

bool write_archive(const char **src, const char *dest) {
  /*
   * Bugfix: Write to <dest>.tmp.<pid> then atomically rename to
   * <dest>. Two parallel dache invocations on the same key write to
   * distinct temp files; whichever renames last wins, and neither
   * sees a half-written archive.
   */
  char tmp_path[PATH_MAX];
  int n =
      snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%ld", dest, (long)getpid());

  if (n < 0 || (size_t)n >= sizeof(tmp_path)) {
    fprintf(stderr, "[dache] archive dest path too long: %s\n", dest);
    return false;
  }

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

  if (archive_write_open_filename(a, tmp_path) != ARCHIVE_OK) {
    fprintf(stderr, "[dache] archive open failed: %s\n",
            archive_error_string(a));
    archive_write_free(a);
    return false;
  }

  struct archive_entry *entry = archive_entry_new();

  if (!entry) {
    archive_write_close(a);
    archive_write_free(a);
    unlink(tmp_path);
    return false;
  }

  bool ok = true;

  archive_entry_set_pathname(entry, ARCHIVE_VERSION_MARKER);
  archive_entry_set_size(entry, 0);
  archive_entry_set_filetype(entry, AE_IFREG);
  archive_entry_set_perm(entry, 0644);

  if (archive_write_header(a, entry) != ARCHIVE_OK) {
    fprintf(stderr, "[dache] archive header failed for version marker: %s\n",
            archive_error_string(a));
    archive_entry_free(entry);
    archive_write_close(a);
    archive_write_free(a);
    unlink(tmp_path);
    return false;
  }

  archive_entry_clear(entry);

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

  if (ok) {
    if (rename(tmp_path, dest) != 0) {
      fprintf(stderr, "[dache] archive rename '%s' -> '%s' failed: %s\n",
              tmp_path, dest, strerror(errno));
      unlink(tmp_path);
      ok = false;
    }
  } else {
    unlink(tmp_path);
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

  /*
   * The first entry MUST be our format-version marker; otherwise this
   * either isn't a dache archive or comes from an incompatible
   * version.  Refusing here is safer than half-extracting unknown
   * content.
   */
  struct archive_entry *entry;
  int r = archive_read_next_header(a, &entry);

  if (r == ARCHIVE_EOF) {
    fprintf(stderr, "[dache] archive is empty: %s\n", src ? src : "(stdin)");
    ok = false;
  } else if (r != ARCHIVE_OK) {
    fprintf(stderr, "%s\n", archive_error_string(a));
    ok = false;
  } else {
    const char *first_name = archive_entry_pathname(entry);

    if (!first_name || strcmp(first_name, ARCHIVE_VERSION_MARKER) != 0) {
      fprintf(stderr,
              "[dache] archive missing format marker (first entry: '%s'); "
              "expected '%s' — refusing to extract\n",
              first_name ? first_name : "", ARCHIVE_VERSION_MARKER);
      ok = false;
    } else {
      archive_read_data_skip(a);

      for (;;) {
        r = archive_read_next_header(a, &entry);

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
