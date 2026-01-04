#include <archive.h>
#include <archive_entry.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <zlib.h>

#include "dache.h"

static const char endline[] = "\n";
static const size_t endline_len = sizeof(endline) - 1;

bool write_archive(const char **src, const char *dest) {
  struct archive *a;
  struct archive_entry *entry;
  struct stat st;
  char buff[8192];
  int len;
  int fd;

  a = archive_write_new();
  archive_write_add_filter_gzip(a);
  archive_write_set_format_pax_restricted(a);

  if (archive_write_open_filename(a, dest) != ARCHIVE_OK) {
    perror(archive_error_string(a));
    return false;
  }

  entry = archive_entry_new();

  while (*src) {
    stat(*src, &st);

    archive_entry_set_pathname(entry, *src);
    archive_entry_set_size(entry, st.st_size);
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, st.st_mode & 0777);
    archive_write_header(a, entry);

    fd = open(*src, O_RDONLY);

    while ((len = read(fd, buff, sizeof(buff))) > 0) {
      archive_write_data(a, buff, len);
    }

    close(fd);
    archive_entry_clear(entry);
    src++;
  }

  archive_entry_free(entry);
  archive_write_close(a);
  archive_write_free(a);

  return true;
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
  struct archive *a;
  struct archive *ext;
  struct archive_entry *entry;
  int r;
  int flags;
  int needcr;

  (void)dest; /* TODO: support extracting to specific directory */

  flags = ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM;

  a = archive_read_new();
  ext = archive_write_disk_new();

  archive_write_disk_set_options(ext, flags);
  archive_read_support_filter_gzip(a);
  archive_read_support_format_tar(a);

  if (src != NULL && strcmp(src, "-") == 0) {
    src = NULL;
  }

  r = archive_read_open_filename(a, src, 10240);
  if (r) {
    fprintf(stderr, "%s\n", archive_error_string(a));
    return false;
  }

  for (;;) {
    needcr = 0;
    r = archive_read_next_header(a, &entry);

    if (r == ARCHIVE_EOF) {
      break;
    }

    if (r != ARCHIVE_OK) {
      fprintf(stderr, "%s\n", archive_error_string(a));
      return false;
    }

    r = archive_write_header(ext, entry);

    if (r != ARCHIVE_OK) {
      fprintf(stderr, "%s\n", archive_error_string(a));
      needcr = 1;
    } else {
      r = copy_data(a, ext);

      if (r != ARCHIVE_OK) {
        needcr = 1;
      }
    }

    if (needcr) {
      write(1, endline, endline_len);
    }
  }

  archive_read_close(a);
  archive_read_free(a);
  archive_write_close(ext);
  archive_write_free(ext);

  return true;
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
