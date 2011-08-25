#ifndef __LIB_STDIO_H
#define __LIB_STDIO_H

#include <lib/debug.h>
#include <lib/stdarg.h>
#include <lib/stdbool.h>
#include <lib/stddef.h>
#include <lib/stdint.h>
#include <lib/types.h>

/* Predefined file handles. */
#define STDIN_FILENO 0
#define STDOUT_FILENO 1

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define EOF (-1)

#define FILE_BLOCK_SIZE 65536

typedef struct FILE {
  char *mode;
  struct disk *disk;
  char sector[FILE_BLOCK_SIZE];
  bool sector_in_memory;
  disk_sector_t disk_sector;
  size_t pos;
} FILE;

FILE *fopen(struct disk *disk, char const *mode);

/* Character functions. */
int fputc(int c, struct FILE *stream);
int fgetc(struct FILE *stream);
int ungetc(int c, struct FILE *stream);

/* read/write. */
size_t fread(void *buf, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *buf, size_t size, size_t nmemb, FILE *stream);

/* seek/tell. */
int fseeko(struct FILE *stream, off_t offset, int whence);
off_t ftello(struct FILE *stream);

/* Standard functions. */
int printf (const char *, ...) PRINTF_FORMAT (1, 2);
int fprintf (FILE *stream, const char *, ...) PRINTF_FORMAT (2, 3);
int snprintf (char *, size_t, const char *, ...) PRINTF_FORMAT (3, 4);
int vprintf (const char *, va_list) PRINTF_FORMAT (1, 0);
int vsnprintf (char *, size_t, const char *, va_list) PRINTF_FORMAT (3, 0);
int putchar (int);
int puts (const char *);

/* scanf. */
int vfscanf(FILE *stream, char const *format, va_list arg_ptr);
int vsscanf(char const *str, char const *format, va_list arg_ptr);
int fscanf(FILE *stream, char const *format, ...);
int sscanf(char const *str, char const *format, ...);

/* flush. */
void fflush(FILE *stream);

/* Nonstandard functions. */
void hex_dump (uintptr_t ofs, const void *, size_t size, bool ascii);
void print_human_readable_size (uint64_t size);

/* Internal functions. */
void __vprintf (const char *format, va_list args,
                void (*output) (char, void *), void *aux);
void __printf (const char *format,
               void (*output) (char, void *), void *aux, ...);

void fputchar(char c, struct FILE *stream);
long strtol(char const *nptr, char **endptr, int base);
long long strtoll(char const *nptr, char **endptr, int base);
int vfprintf(FILE *stream, char const *format, va_list args);
void file_check(FILE *stream);

/* Try to be helpful. */
#define sprintf dont_use_sprintf_use_snprintf
#define vsprintf dont_use_vsprintf_use_vsnprintf

#endif /* lib/stdio.h */
