#include <stdio.h>
#include <ctype.h>
#include <inttypes.h>
#include <round.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include "sys/vcpu.h"
#include "mem/malloc.h"
#include "devices/disk.h"
#include "sys/interrupt.h"
#include "sys/mode.h"
#include "sys/rr_log.h"

/* Auxiliary data for vsnprintf_helper(). */
struct vsnprintf_aux 
  {
    char *p;            /* Current output position. */
    int length;         /* Length of output string. */
    int max_length;     /* Max length of output string. */
  };

static void vsnprintf_helper (char, void *);

/* Like vprintf(), except that output is stored into BUFFER,
   which must have space for BUF_SIZE characters.  Writes at most
   BUF_SIZE - 1 characters to BUFFER, followed by a null
   terminator.  BUFFER will always be null-terminated unless
   BUF_SIZE is zero.  Returns the number of characters that would
   have been written to BUFFER, not including a null terminator,
   had there been enough room. */
int
vsnprintf (char *buffer, size_t buf_size, const char *format, va_list args) 
{
  /* Set up aux data for vsnprintf_helper(). */
  struct vsnprintf_aux aux;
  aux.p = buffer;
  aux.length = 0;
  aux.max_length = buf_size > 0 ? buf_size - 1 : 0;

  /* Do most of the work. */
  __vprintf (format, args, vsnprintf_helper, &aux);

  /* Add null terminator. */
  if (buf_size > 0)
    *aux.p = '\0';

  return aux.length;
}

/* Helper function for vsnprintf(). */
static void
vsnprintf_helper (char ch, void *aux_)
{
  struct vsnprintf_aux *aux = aux_;

  if (aux->length++ < aux->max_length)
    *aux->p++ = ch;
}

/* Like printf(), except that output is stored into BUFFER,
   which must have space for BUF_SIZE characters.  Writes at most
   BUF_SIZE - 1 characters to BUFFER, followed by a null
   terminator.  BUFFER will always be null-terminated unless
   BUF_SIZE is zero.  Returns the number of characters that would
   have been written to BUFFER, not including a null terminator,
   had there been enough room. */
int
snprintf (char *buffer, size_t buf_size, const char *format, ...) 
{
  va_list args;
  int retval;

  va_start (args, format);
  retval = vsnprintf (buffer, buf_size, format, args);
  va_end (args);

  return retval;
}

/* Writes formatted output to the console.
   In the kernel, the console is both the video display and first
   serial port.
   In userspace, the console is file descriptor 1. */
int
printf (const char *format, ...) 
{
  va_list args;
  int retval;

  va_start (args, format);
  retval = vprintf (format, args);
  va_end (args);

  return retval;
}

/* Writes formatted output to the stream. */
int
fprintf (FILE *stream, const char *format, ...) 
{
  va_list args;
  int retval;

  va_start (args, format);
  retval = vfprintf (stream, format, args);
  va_end (args);

  return retval;
}



/* printf() formatting internals. */

/* A printf() conversion. */
struct printf_conversion 
  {
    /* Flags. */
    enum 
      {
        MINUS = 1 << 0,         /* '-' */
        PLUS = 1 << 1,          /* '+' */
        SPACE = 1 << 2,         /* ' ' */
        POUND = 1 << 3,         /* '#' */
        ZERO = 1 << 4,          /* '0' */
        GROUP = 1 << 5          /* '\'' */
      }
    flags;

    /* Minimum field width. */
    int width;

    /* Numeric precision.
       -1 indicates no precision was specified. */
    int precision;

    /* Type of argument to format. */
    enum 
      {
        CHAR = 1,               /* hh */
        SHORT = 2,              /* h */
        INT = 3,                /* (none) */
        INTMAX = 4,             /* j */
        LONG = 5,               /* l */
        LONGLONG = 6,           /* ll */
        PTRDIFFT = 7,           /* t */
        SIZET = 8               /* z */
      }
    type;
  };

struct integer_base 
  {
    int base;                   /* Base. */
    const char *digits;         /* Collection of digits. */
    int x;                      /* `x' character to use, for base 16 only. */
    int group;                  /* Number of digits to group with ' flag. */
  };

static const struct integer_base base_d = {10, "0123456789", 0, 3};
static const struct integer_base base_o = {8, "01234567", 0, 3};
static const struct integer_base base_x = {16, "0123456789abcdef", 'x', 4};
static const struct integer_base base_X = {16, "0123456789ABCDEF", 'X', 4};

static const char *parse_conversion (const char *format,
                                     struct printf_conversion *,
                                     va_list *);
static void format_integer (uintmax_t value, bool is_signed, bool negative, 
                            const struct integer_base *,
                            const struct printf_conversion *,
                            void (*output) (char, void *), void *aux);
static void output_dup (char ch, size_t cnt,
                        void (*output) (char, void *), void *aux);
static void format_string (const char *string, int length,
                           struct printf_conversion *,
                           void (*output) (char, void *), void *aux);

void
__vprintf (const char *format, va_list args,
           void (*output) (char, void *), void *aux)
{
  for (; *format != '\0'; format++)
    {
      struct printf_conversion c;

      /* Literally copy non-conversions to output. */
      if (*format != '%') 
        {
          output (*format, aux);
          continue;
        }
      format++;

      /* %% => %. */
      if (*format == '%') 
        {
          output ('%', aux);
          continue;
        }

      /* Parse conversion specifiers. */
      format = parse_conversion (format, &c, &args);

      /* Do conversion. */
      switch (*format) 
        {
        case 'd':
        case 'i': 
          {
            /* Signed integer conversions. */
            intmax_t value;
            
            switch (c.type) 
              {
              case CHAR: 
                value = (signed char) va_arg (args, int);
                break;
              case SHORT:
                value = (short) va_arg (args, int);
                break;
              case INT:
                value = va_arg (args, int);
                break;
              case INTMAX:
                value = va_arg (args, intmax_t);
                break;
              case LONG:
                value = va_arg (args, long);
                break;
              case LONGLONG:
                value = va_arg (args, long long);
                break;
              case PTRDIFFT:
                value = va_arg (args, ptrdiff_t);
                break;
              case SIZET:
                value = va_arg (args, size_t);
                if (value > SIZE_MAX / 2)
                  value = value - SIZE_MAX - 1;
                break;
              default:
                NOT_REACHED ();
              }

            format_integer (value < 0 ? -value : value,
                            true, value < 0, &base_d, &c, output, aux);
          }
          break;
          
        case 'o':
        case 'u':
        case 'x':
        case 'X':
          {
            /* Unsigned integer conversions. */
            uintmax_t value;
            const struct integer_base *b;

            switch (c.type) 
              {
              case CHAR: 
                value = (unsigned char) va_arg (args, unsigned);
                break;
              case SHORT:
                value = (unsigned short) va_arg (args, unsigned);
                break;
              case INT:
                value = va_arg (args, unsigned);
                break;
              case INTMAX:
                value = va_arg (args, uintmax_t);
                break;
              case LONG:
                value = va_arg (args, unsigned long);
                break;
              case LONGLONG:
                value = va_arg (args, unsigned long long);
                break;
              case PTRDIFFT:
                value = va_arg (args, ptrdiff_t);
#if UINTMAX_MAX != PTRDIFF_MAX
                value &= ((uintmax_t) PTRDIFF_MAX << 1) | 1;
#endif
                break;
              case SIZET:
                value = va_arg (args, size_t);
                break;
              default:
                NOT_REACHED ();
              }

            switch (*format) 
              {
              case 'o': b = &base_o; break;
              case 'u': b = &base_d; break;
              case 'x': b = &base_x; break;
              case 'X': b = &base_X; break;
              default: NOT_REACHED ();
              }

            format_integer (value, false, false, b, &c, output, aux);
          }
          break;

        case 'c': 
          {
            /* Treat character as single-character string. */
            char ch = va_arg (args, int);
            format_string (&ch, 1, &c, output, aux);
          }
          break;

        case 's':
          {
            /* String conversion. */
            const char *s = va_arg (args, char *);
            if (s == NULL)
              s = "(null)";

            /* Limit string length according to precision.
               Note: if c.precision == -1 then strnlen() will get
               SIZE_MAX for MAXLEN, which is just what we want. */
            format_string (s, strnlen (s, c.precision), &c, output, aux);
          }
          break;
          
        case 'p':
          {
            /* Pointer conversion.
               Format pointers as %#x. */
            void *p = va_arg (args, void *);

            c.flags = POUND;
            format_integer ((uintptr_t) p, false, false,
                            &base_x, &c, output, aux);
          }
          break;
      
        case 'f':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
        case 'n':
          /* We don't support floating-point arithmetic,
             and %n can be part of a security hole. */
          __printf ("<<no %%%c in kernel>>", output, aux, *format);
          break;

        default:
          __printf ("<<no %%%c conversion>>", output, aux, *format);
          break;
        }
    }
}

/* Parses conversion option characters starting at FORMAT and
   initializes C appropriately.  Returns the character in FORMAT
   that indicates the conversion (e.g. the `d' in `%d').  Uses
   *ARGS for `*' field widths and precisions. */
static const char *
parse_conversion (const char *format, struct printf_conversion *c,
                  va_list *args) 
{
  /* Parse flag characters. */
  c->flags = 0;
  for (;;) 
    {
      switch (*format++) 
        {
        case '-':
          c->flags |= MINUS;
          break;
        case '+':
          c->flags |= PLUS;
          break;
        case ' ':
          c->flags |= SPACE;
          break;
        case '#':
          c->flags |= POUND;
          break;
        case '0':
          c->flags |= ZERO;
          break;
        case '\'':
          c->flags |= GROUP;
          break;
        default:
          format--;
          goto not_a_flag;
        }
    }
 not_a_flag:
  if (c->flags & MINUS)
    c->flags &= ~ZERO;
  if (c->flags & PLUS)
    c->flags &= ~SPACE;

  /* Parse field width. */
  c->width = 0;
  if (*format == '*')
    {
      format++;
      c->width = va_arg (*args, int);
    }
  else 
    {
      for (; isdigit (*format); format++)
        c->width = c->width * 10 + *format - '0';
    }
  if (c->width < 0) 
    {
      c->width = -c->width;
      c->flags |= MINUS;
    }
      
  /* Parse precision. */
  c->precision = -1;
  if (*format == '.') 
    {
      format++;
      if (*format == '*') 
        {
          format++;
          c->precision = va_arg (*args, int);
        }
      else 
        {
          c->precision = 0;
          for (; isdigit (*format); format++)
            c->precision = c->precision * 10 + *format - '0';
        }
      if (c->precision < 0) 
        c->precision = -1;
    }
  if (c->precision >= 0)
    c->flags &= ~ZERO;

  /* Parse type. */
  c->type = INT;
  switch (*format++) 
    {
    case 'h':
      if (*format == 'h') 
        {
          format++;
          c->type = CHAR;
        }
      else
        c->type = SHORT;
      break;
      
    case 'j':
      c->type = INTMAX;
      break;

    case 'l':
      if (*format == 'l')
        {
          format++;
          c->type = LONGLONG;
        }
      else
        c->type = LONG;
      break;

    case 't':
      c->type = PTRDIFFT;
      break;

    case 'z':
      c->type = SIZET;
      break;

    default:
      format--;
      break;
    }

  return format;
}

/* Performs an integer conversion, writing output to OUTPUT with
   auxiliary data AUX.  The integer converted has absolute value
   VALUE.  If IS_SIGNED is true, does a signed conversion with
   NEGATIVE indicating a negative value; otherwise does an
   unsigned conversion and ignores NEGATIVE.  The output is done
   according to the provided base B.  Details of the conversion
   are in C. */
static void
format_integer (uintmax_t value, bool is_signed, bool negative, 
                const struct integer_base *b,
                const struct printf_conversion *c,
                void (*output) (char, void *), void *aux)
{
  char buf[64], *cp;            /* Buffer and current position. */
  int x;                        /* `x' character to use or 0 if none. */
  int sign;                     /* Sign character or 0 if none. */
  int precision;                /* Rendered precision. */
  int pad_cnt;                  /* # of pad characters to fill field width. */
  int digit_cnt;                /* # of digits output so far. */

  /* Determine sign character, if any.
     An unsigned conversion will never have a sign character,
     even if one of the flags requests one. */
  sign = 0;
  if (is_signed) 
    {
      if (c->flags & PLUS)
        sign = negative ? '-' : '+';
      else if (c->flags & SPACE)
        sign = negative ? '-' : ' ';
      else if (negative)
        sign = '-';
    }

  /* Determine whether to include `0x' or `0X'.
     It will only be included with a hexadecimal conversion of a
     nonzero value with the # flag. */
  x = (c->flags & POUND) && value ? b->x : 0;

  /* Accumulate digits into buffer.
     This algorithm produces digits in reverse order, so later we
     will output the buffer's content in reverse. */
  cp = buf;
  digit_cnt = 0;
  while (value > 0) 
    {
      if ((c->flags & GROUP) && digit_cnt > 0 && digit_cnt % b->group == 0)
        *cp++ = ',';
      *cp++ = b->digits[value % b->base];
      value /= b->base;
      digit_cnt++;
    }

  /* Append enough zeros to match precision.
     If requested precision is 0, then a value of zero is
     rendered as a null string, otherwise as "0".
     If the # flag is used with base 8, the result must always
     begin with a zero. */
  precision = c->precision < 0 ? 1 : c->precision;
  while (cp - buf < precision && cp < buf + sizeof buf - 1)
    *cp++ = '0';
  if ((c->flags & POUND) && b->base == 8 && (cp == buf || cp[-1] != '0'))
    *cp++ = '0';

  /* Calculate number of pad characters to fill field width. */
  pad_cnt = c->width - (cp - buf) - (x ? 2 : 0) - (sign != 0);
  if (pad_cnt < 0)
    pad_cnt = 0;

  /* Do output. */
  if ((c->flags & (MINUS | ZERO)) == 0)
    output_dup (' ', pad_cnt, output, aux);
  if (sign)
    output (sign, aux);
  if (x) 
    {
      output ('0', aux);
      output (x, aux); 
    }
  if (c->flags & ZERO)
    output_dup ('0', pad_cnt, output, aux);
  while (cp > buf)
    output (*--cp, aux);
  if (c->flags & MINUS)
    output_dup (' ', pad_cnt, output, aux);
}

/* Writes CH to OUTPUT with auxiliary data AUX, CNT times. */
static void
output_dup (char ch, size_t cnt, void (*output) (char, void *), void *aux) 
{
  while (cnt-- > 0)
    output (ch, aux);
}

/* Formats the LENGTH characters starting at STRING according to
   the conversion specified in C.  Writes output to OUTPUT with
   auxiliary data AUX. */
static void
format_string (const char *string, int length,
               struct printf_conversion *c,
               void (*output) (char, void *), void *aux) 
{
  int i;
  if (c->width > length && (c->flags & MINUS) == 0)
    output_dup (' ', c->width - length, output, aux);
  for (i = 0; i < length; i++)
    output (string[i], aux);
  if (c->width > length && (c->flags & MINUS) != 0)
    output_dup (' ', c->width - length, output, aux);
}

/* Wrapper for __vprintf() that converts varargs into a
   va_list. */
void
__printf (const char *format,
          void (*output) (char, void *), void *aux, ...) 
{
  va_list args;

  va_start (args, aux);
  __vprintf (format, args, output, aux);
  va_end (args);
}

/* Dumps the SIZE bytes in BUF to the console as hex bytes
   arranged 16 per line.  Numeric offsets are also included,
   starting at OFS for the first byte in BUF.  If ASCII is true
   then the corresponding ASCII characters are also rendered
   alongside. */   
void
hex_dump (uintptr_t ofs, const void *buf_, size_t size, bool ascii)
{
  const uint8_t *buf = buf_;
  const size_t per_line = 16; /* Maximum bytes per line. */

  while (size > 0)
    {
      size_t start, end, n;
      size_t i;
      
      /* Number of bytes on this line. */
      start = ofs % per_line;
      end = per_line;
      if (end - start > size)
        end = start + size;
      n = end - start;

      /* Print line. */
      printf ("%08jx  ", (uintmax_t) ROUND_DOWN (ofs, per_line));
      for (i = 0; i < start; i++)
        printf ("   ");
      for (; i < end; i++) 
        printf ("%02hhx%c",
                buf[i - start], i == per_line / 2 - 1? '-' : ' ');
      if (ascii) 
        {
          for (; i < per_line; i++)
            printf ("   ");
          printf ("|");
          for (i = 0; i < start; i++)
            printf (" ");
          for (; i < end; i++)
            printf ("%c",
                    isprint (buf[i - start]) ? buf[i - start] : '.');
          for (; i < per_line; i++)
            printf (" ");
          printf ("|");
        }
      printf ("\n");

      ofs += n;
      buf += n;
      size -= n;
    }
}


/* *****************************************************************************
 * * Copyright (C) 2002,2006 by Jeroen van der Zijp.   All Rights Reserved.        *
 * *****************************************************************************
 * * This library is free software; you can redistribute it and/or             *
 * * modify it under the terms of the GNU Lesser General Public                *
 * * License as published by the Free Software Foundation; either              *
 * * version 2.1 of the License, or (at your option) any later version.        *
 * *                                                                           *
 * * This library is distributed in the hope that it will be useful,           *
 * * but WITHOUT ANY WARRANTY; without even the implied warranty of            *
 * * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU         *
 * * Lesser General Public License for more details.                           *
 * *                                                                           *
 * * You should have received a copy of the GNU Lesser General Public          *
 * * License along with this library; if not, write to the Free Software       *
 * * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.*
 * *****************************************************************************
 * * $Id: vsscanf.cpp,v 1.20.2.2 2007/01/29 20:22:29 fox Exp $                 *
 * *****************************************************************************
 *
 * scanf() functions taken from
 * http://ftp2.ru.freebsd.org/pub/FreeBSD/distfiles/fox-1.6.25.tar.gz
 */

/*
 * Notes:
 * - Needs checking for conformance with standard scanf.
 * - Some POSIX finesse:
 *   u,d is equivalent to strtol (strtoul) with base = 10.
 *   x   is equivalent to strtoul with base = 16
 *   o   is equivalent to strtoul with base = 8
 *   i   is equivalent to strtol with base = 0 (which means either
 *   octal, hex, or decimal as determined by leading 2 characters).
 * - Rewrite in terms of strtol, strtoul strtod; these are available since we're
 *   already using them and have heard no complaints.
 */

typedef struct arg_scanf {
  void *data;
  int (*getch)(void*);
  int (*putch)(int,void*);
} arg_scanf;

typedef struct str_data {
  unsigned char* str;
} str_data;


static int
sgetc(struct str_data* sd)
{
  register unsigned int ret = *sd->str++;
  return ret ? (int)ret : -1;
}

static int
sputc(int c,struct str_data* sd)
{
  return (*--sd->str==c)?c:-1;
}

int
fgetc(struct FILE *stream)
{
  ASSERT(!strcmp(stream->mode, "r") || !strcmp(stream->mode, "rw"));
  if (!stream->sector_in_memory) {
    disk_read(stream->disk, stream->disk_sector,
        FILE_BLOCK_SIZE/DISK_SECTOR_SIZE, stream->sector);
    stream->pos = 0;
    stream->sector_in_memory = true;
  }
  if (stream->pos < FILE_BLOCK_SIZE) {
    return stream->sector[stream->pos++];
  }
  stream->disk_sector += FILE_BLOCK_SIZE/DISK_SECTOR_SIZE;
  if (stream->disk_sector > disk_size(stream->disk)) {
    return EOF;
  }
  disk_read(stream->disk, stream->disk_sector,
      FILE_BLOCK_SIZE/DISK_SECTOR_SIZE, stream->sector);
  stream->sector_in_memory = true;
  stream->pos = 0;
  return stream->sector[stream->pos++];
}

int 
ungetc(int c, struct FILE *stream)
{
  ASSERT(!strcmp(stream->mode, "r") || !strcmp(stream->mode, "rw"));
  ASSERT(stream->pos > 0);
  stream->pos--;
  ASSERT(stream->sector[stream->pos] == c);
  return 0;
}

int
fputc(int c, FILE *stream)
{
  int i;

  ASSERT(stream);
  ASSERT(stream->disk);
  ASSERT(!strcmp(stream->mode, "w") || !strcmp(stream->mode, "rw"));
  //ASSERT(stream->sector_in_memory == false);
  if (stream->pos < FILE_BLOCK_SIZE) {
    stream->sector[stream->pos++] = c;
    return 0;
  }
  disk_write(stream->disk, stream->disk_sector,
      FILE_BLOCK_SIZE/DISK_SECTOR_SIZE, stream->sector);
  stream->pos = 0;
  stream->disk_sector += FILE_BLOCK_SIZE/DISK_SECTOR_SIZE;
  if (stream->disk_sector > disk_size(stream->disk)) {
    return EOF;
  }
  stream->sector[stream->pos++] = c;
  return 0;
}

void
fflush(FILE *stream)
{
  if (!strcmp(stream->mode, "w") || !strcmp(stream->mode, "rw")) {
    disk_sector_t disk_sectors_finished_writing;

    //ASSERT(!stream->sector_in_memory);
    disk_write(stream->disk, stream->disk_sector,
        (stream->pos + DISK_SECTOR_SIZE - 1)/DISK_SECTOR_SIZE, stream->sector);
    disk_sectors_finished_writing = stream->pos/DISK_SECTOR_SIZE;
    stream->disk_sector += disk_sectors_finished_writing;
    stream->pos = (stream->pos % DISK_SECTOR_SIZE);
    memmove(stream->sector,
        &stream->sector[disk_sectors_finished_writing*DISK_SECTOR_SIZE],
        stream->pos);
  }
}

void
fputchar(char c, struct FILE *stream)
{
  fputc(c, stream);
}

/*
 * Convert a string to a long integer.
 *
 * Ignores `locale' stuff.  Assumes that the upper and lower case
 * alphabets and digits are each contiguous.
 *
 * taken from git://android.git.kernel.org/platform/bionic.git/
 */
long
strtol(char const *nptr, char **endptr, int base)
{
  const char *s;
  long acc, cutoff;
  int c;
  int neg, any, cutlim;

  /*
   * Skip white space and pick up leading +/- sign if any.
   * If base is 0, allow 0x for hex and 0 for octal, else
   * assume decimal; if base is already 16, allow 0x.
   */
  s = nptr;
  do {
    c = (unsigned char) *s++;
  } while (isspace(c));
  if (c == '-') {
    neg = 1;
    c = *s++;
  } else {
    neg = 0;
    if (c == '+')
      c = *s++;
  }
  if ((base == 0 || base == 16) &&
      c == '0' && (*s == 'x' || *s == 'X')) {
    c = s[1];
    s += 2;
    base = 16;
  }
  if (base == 0)
    base = c == '0' ? 8 : 10;

  /*
   * Compute the cutoff value between legal numbers and illegal
   * numbers.  That is the largest legal value, divided by the
   * base.  An input number that is greater than this value, if
   * followed by a legal input character, is too big.  One that
   * is equal to this value may be valid or not; the limit
   * between valid and invalid numbers is then based on the last
   * digit.  For instance, if the range for longs is
   * [-2147483648..2147483647] and the input base is 10,
   * cutoff will be set to 214748364 and cutlim to either
   * 7 (neg==0) or 8 (neg==1), meaning that if we have accumulated
   * a value > 214748364, or equal but the next digit is > 7 (or 8),
   * the number is too big, and we will return a range error.
   *
   * Set any if any `digits' consumed; make it negative to indicate
   * overflow.
   */
  cutoff = neg ? LONG_MIN : LONG_MAX;
  cutlim = cutoff % base;
  cutoff /= base;
  if (neg) {
    if (cutlim > 0) {
      cutlim -= base;
      cutoff += 1;
    }
    cutlim = -cutlim;
  }
  for (acc = 0, any = 0;; c = (unsigned char) *s++) {
    if (isdigit(c))
      c -= '0';
    else if (isalpha(c))
      c -= isupper(c) ? 'A' - 10 : 'a' - 10;
    else
      break;
    if (c >= base)
      break;
    if (any < 0)
      continue;
    if (neg) {
      if (acc < cutoff || (acc == cutoff && c > cutlim)) {
        any = -1;
        acc = LONG_MIN;
        //errno = ERANGE;
      } else {
        any = 1;
        acc *= base;
        acc -= c;
      }
    } else {
      if (acc > cutoff || (acc == cutoff && c > cutlim)) {
        any = -1;
        acc = LONG_MAX;
        //errno = ERANGE;
      } else {
        any = 1;
        acc *= base;
        acc += c;
      }
    }
  }
  if (endptr != 0)
    *endptr = (char *) (any ? s - 1 : nptr);
  return (acc);
}


long long
strtoll(char const *nptr, char **endptr, int base)
{
  /* XXX: fix this. */
  return strtol(nptr, endptr, base);
}

#define A_GETC(fn)      (++consumed,(fn)->getch((fn)->data))
#define A_PUTC(c,fn)    (--consumed,(fn)->putch((c),(fn)->data))


static int
__vscanf(arg_scanf* fn, const char *format, va_list arg_ptr)
{
  int flag_not,flag_dash,flag_width,flag_discard,flag_half,flag_long,
      flag_longlong;
  unsigned int ch,_div,consumedsofar,consumed;
  unsigned long v;
  //double d,factor;
  int width,n,neg,exp,prec,tpch;
  char cset[256],*s;

  /* arg_ptr tmps */
  //double *pd;
  //float  *pf;
  long   *pl;
  short  *ph;
  int    *pi;

  consumed=0;
  n=0;

  /* get one char */
  tpch=A_GETC(fn);

  while(tpch!=-1 && *format){
    ch=(unsigned int)*format++;
    switch(ch){
      case 0:                                        // End of the format string
        return 0;
      case ' ':                                      // Skip blanks
      case '\f':
      case '\t':
      case '\v':
      case '\n':
      case '\r':
        while(*format && isspace(*format)) ++format;
        while(isspace(tpch)) tpch=A_GETC(fn);
        break;
      case '%':                                      // Format string %
        _div=0;
        width=-1;
        flag_width=0;
        flag_discard=0;
        flag_half=0;
        flag_long=0;
        flag_longlong=0;
in_scan:ch=*format++;
        switch(ch){
          case 0:                                    // End of the format string
            return 0;
          case '%':                                  // Just a % sign
            if((unsigned char)tpch != ch) goto err_out;
            tpch=A_GETC(fn);
            break;
          case '*':                               // Discard, i.e. don't convert
            flag_discard=1;
            goto in_scan;
          case 'h':                               // Argument is short
            flag_half=1;
            goto in_scan;
          case 'l':                             // Argument is long or long long
            if(flag_long) flag_longlong=1;
            flag_long=1;
            goto in_scan;
          case 'q':                             // Argument is long long
          case 'L':
            flag_longlong=1;
            goto in_scan;
          case '0':                             // Width specification
          case '1':
          case '2':
          case '3':
          case '4':
          case '5':
          case '6':
          case '7':
          case '8':
          case '9':
            width=strtol(format-1,&s,10);
            format=s;
            flag_width=1;
            goto in_scan;
          case 'p':                                     // Pointer (hex)
          case 'X':
          case 'x':                                     // Hexadecimal
            _div+=6;
          case 'd':                                     // Decimal
          case 'u':
            _div+=2;
          case 'o':                                     // Octal
            _div+=8;
          case 'i':                        // 'i' may be decimal, octal, or hex
            v=0;
            consumedsofar=consumed;
            while(isspace(tpch)) tpch=A_GETC(fn);
            neg=0;
            if(tpch=='-'){
              tpch=A_GETC(fn);
              neg=1;
            }
            else if(tpch=='+'){
              tpch=A_GETC(fn);
            }
            if((_div==16) && (tpch=='0')) goto scan_hex;
            if(!_div){
              _div=10;
              if(tpch=='0'){
                _div=8;
scan_hex:       tpch=A_GETC(fn);
                if((tpch|32)=='x'){
                  tpch=A_GETC(fn);
                  _div=16;
                }
              }
            }
            while(tpch!=-1){
              register unsigned long c=tpch&0xff;
              register unsigned long d=c|0x20;
              c=(d>='a'?d-'a'+10:c<='9'?c-'0':0xff);
              if(c>=_div) break;
              d=v*_div;
              v=(d<v)?ULONG_MAX:d+c;
              tpch=A_GETC(fn);
            }
            if((ch|0x20)<'p'){
              if(v>=0-((unsigned long)LONG_MIN)){
                v=(neg)?LONG_MIN:LONG_MAX;
              }
              else{
                if(neg) v*=-1L;
              }
            }
            if(!flag_discard){
              if(flag_long){
                pl=(long *)va_arg(arg_ptr,long*);
                *pl=v;
              }
              else if(flag_half){
                ph=(short*)va_arg(arg_ptr,short*);
                *ph=(short)v;
              }
              else{     // FIXME handle flag_longlong
                pi=(int*)va_arg(arg_ptr,int*);
                *pi=(int)v;
              }
              if(consumedsofar<consumed) ++n;
            }
            break;
#if 0
          case 'e':                                     // Floating point
          case 'E':
          case 'f':
          case 'g':
            d=0.0;
            while(isspace(tpch)) tpch=A_GETC(fn);
            neg=0;
            if(tpch=='-'){
              tpch=A_GETC(fn);
              neg=1;
            }
            else if(tpch=='+'){
              tpch=A_GETC(fn);
            }
            while(isDigit(tpch)){
              d=d*10.0+(tpch-'0');
              tpch=A_GETC(fn);
            }
            if(tpch=='.'){
              factor=.1;
              tpch=A_GETC(fn);
              while(isDigit(tpch)){
                d=d+(factor*(tpch-'0'));
                factor/=10;
                tpch=A_GETC(fn);
              }
            }
            if((tpch|0x20)=='e'){
              exp=0;
              prec=tpch;
              tpch=A_GETC(fn);
              factor=10;
              if(tpch=='-'){
                factor=0.1;
                tpch=A_GETC(fn);
              }
              else if(tpch=='+'){
                tpch=A_GETC(fn);
              }
              else{
                d=0;
                if(tpch!=-1) A_PUTC(tpch,fn);
                tpch=prec;
                goto exp_out;
              }
              while(isDigit(tpch)){
                exp=exp*10+(tpch-'0');
                tpch=A_GETC(fn);
              }
              while(exp){/* as in strtod: XXX: this introduces rounding errors*/
                d*=factor;
                --exp;
              }
            }
exp_out:    if(neg) d=-d;
            if(!flag_discard){
              if(flag_long){
                pd=(double *)va_arg(arg_ptr,double*);
                *pd=d;
              }
              else {
                pf=(float *)va_arg(arg_ptr,float*);
                *pf=(float)d;
              }
              ++n;
            }
            break;
#endif
          case 'c':                                     // Character
            if(!flag_discard){
              s=(char *)va_arg(arg_ptr,char*);
              ++n;
            }
            if(!flag_width) width=1;
            while(width && (tpch!=-1)){
              if(!flag_discard) *s++=tpch;
              --width;
              tpch=A_GETC(fn);
            }
            break;
          case 's':                                     // String
            if(!flag_discard) s=(char *)va_arg(arg_ptr,char*);
            while(isspace(tpch)) tpch=A_GETC(fn);
            while (width && (tpch!=-1) && (!isspace(tpch))){
              if(!flag_discard) *s=tpch;
              if(tpch) ++s; else break;
              --width;
              tpch=A_GETC(fn);
            }
            if(!flag_discard){ *s=0; n++; }
            break;
          case 'n':            // Total characters so far, not including %n's
            if(!flag_discard){
              s=(char *)va_arg(arg_ptr,char*);
            }
            if(!flag_discard) *s++=consumed-1;
            break;
          case '[':                                     // Character set
            memset(cset,0,sizeof(cset));
            ch=*format;
            flag_not=0;
            if(ch=='^'){                                // Negated character set
              flag_not=1;
              ch=*++format;
            }
            if(ch=='-' || ch==']'){        // Special case if first is - or ]
              cset[ch]=1;
              ch=*++format;
            }
            flag_dash=0;
            for( ; *format && *format!=']'; ++format){  // Parse set
              if(flag_dash){
                // Set characters
                for( ; ch<=(unsigned int)*format; ++ch) cset[ch]=1;
                flag_dash=0;
                ch=*format;
              }
              else if(*format=='-'){                    // Character range
                flag_dash=1;
              }
              else{                                     // Set single character
                cset[ch]=1;
                ch=*format;
              }
            }
            if(flag_dash)                               // Last character
              cset[(int)'-']=1;
            else
              cset[ch]=1;
            if(!flag_discard){                   // Copy string if not discarded
              s=(char *)va_arg(arg_ptr,char*);
              ++n;
            }
            while(width && (tpch>=0) && (cset[tpch]^flag_not)){
              if(!flag_discard) *s=tpch;
              if(tpch) ++s; else break;
              --width;
              tpch=A_GETC(fn);
            }
            if(!flag_discard) *s=0;
            ++format;
            break;
          default:
            goto err_out;
        }
        break;
      default:                  // Other characters must match format string
        if((unsigned char)tpch != ch) goto err_out;
        tpch=A_GETC(fn);
        break;
    }
  }
err_out:
  if(tpch<0 && n==0) return EOF;
  A_PUTC(tpch,fn);
  return n;
}


int vfscanf(FILE *stream, const char *format, va_list arg_ptr){
  arg_scanf farg={(void*)stream,(int(*)(void*))fgetc,(int(*)(int,void*))ungetc};
  return __vscanf(&farg,format,arg_ptr);
}

// API
int vsscanf(const char* str,const char* format,va_list arg_ptr){
  str_data fdat={(unsigned char*)str};
  arg_scanf farg={(void*)&fdat,(int(*)(void*))sgetc,(int(*)(int,void*))sputc};
  return __vscanf(&farg,format,arg_ptr);
}

int fscanf(FILE *stream, char const *format, ...)
{
  va_list args;
  int retval;

  va_start(args, format);
  retval = vfscanf(stream, format, args);
  va_end(args);

  return retval;
}

int sscanf(char const *str, char const *format, ...)
{
  va_list args;
  int retval;

  va_start(args, format);
  retval = vsscanf(str, format, args);
  va_end(args);

  return retval;
}

FILE *
fopen(struct disk *disk, char const *mode)
{
  FILE *stream;
  
  ASSERT(disk);
  stream = malloc(sizeof(struct FILE));
  stream->disk = disk;
  stream->pos = 0;
  stream->disk_sector = 0;
  //stream->disk_sector = 0x200000;   //2M*DISK_SECTOR_SIZE=1G for hardware tests
  stream->sector_in_memory = false;
  stream->mode = malloc(strlen(mode) + 1);
  strlcpy(stream->mode, mode, strlen(mode) + 1);
  ASSERT((FILE_BLOCK_SIZE % DISK_SECTOR_SIZE) == 0);
  return stream;
}

/* do not use locks, because only one thread accesses this code. may need
 * to introduce locks (with depth) later.
 */
int
vfprintf(FILE *stream, char const *format, va_list args)
{
  __vprintf(format, args, (void (*)(char, void *))&fputchar, stream);
	return 0;
}

size_t
fread(void *buf, size_t size, size_t nmemb, FILE *stream)
{
  size_t count = size * nmemb;
  uint8_t *ptr = buf;
  uint8_t *end = ptr + count;

  ASSERT(!strcmp(stream->mode, "r") || !strcmp(stream->mode, "rw"));
  do {
    size_t num_read;
    if (!stream->sector_in_memory) {
      disk_read(stream->disk, stream->disk_sector,
          FILE_BLOCK_SIZE/DISK_SECTOR_SIZE, stream->sector);
      stream->pos = 0;
      stream->sector_in_memory = true;
    }
    ASSERT(stream->pos <= FILE_BLOCK_SIZE);
    if (stream->pos == FILE_BLOCK_SIZE) {
      stream->disk_sector += FILE_BLOCK_SIZE/DISK_SECTOR_SIZE;
      if (stream->disk_sector > disk_size(stream->disk)) {
        return (ptr - (uint8_t *)buf)/size;
      }
      disk_read(stream->disk, stream->disk_sector,
          FILE_BLOCK_SIZE/DISK_SECTOR_SIZE, stream->sector);
      stream->sector_in_memory = true;
      stream->pos = 0;
    }
    num_read = min(end - ptr, (int)(FILE_BLOCK_SIZE - stream->pos));
    memcpy(ptr, &stream->sector[stream->pos], num_read);
    ptr += num_read;
    stream->pos += num_read;
  } while(ptr < end);
  return (ptr - (uint8_t *)buf)/size;
}

size_t
fwrite(const void *buf, size_t size, size_t nmemb, FILE *stream)
{
  size_t count = size * nmemb;
  size_t num_write;
  uint8_t const *ptr = buf;
  uint8_t const *end = ptr + count;

  ASSERT(!strcmp(stream->mode, "w") || !strcmp(stream->mode, "rw"));
  //ASSERT(stream->sector_in_memory == false);
  do {
    if (stream->pos < FILE_BLOCK_SIZE) {
      num_write = min(end - ptr, (int)(FILE_BLOCK_SIZE - stream->pos));
      memcpy(&stream->sector[stream->pos], ptr, num_write);
      stream->pos += num_write;
      ptr += num_write;
    } else {
			//printf("writing sector 0x%x\n", (uint32_t)stream->disk_sector);
      disk_write(stream->disk, stream->disk_sector,
          FILE_BLOCK_SIZE/DISK_SECTOR_SIZE, stream->sector);
      stream->pos = 0;
      stream->disk_sector += FILE_BLOCK_SIZE/DISK_SECTOR_SIZE;
      if (stream->disk_sector > disk_size(stream->disk)) {
        return (ptr - (uint8_t *)buf)/size;
      }
      num_write = min(end - ptr, FILE_BLOCK_SIZE);
      memcpy(&stream->sector[stream->pos], ptr, num_write);
      stream->pos += num_write;
      ptr += num_write;
    }
  } while (ptr < end);
  return (ptr - (uint8_t *)buf)/size;
}

int
fseeko(struct FILE *stream, off_t offset, int whence)
{
	if (whence != SEEK_SET) {
		NOT_IMPLEMENTED();
	}
	stream->disk_sector = ((int)(offset/FILE_BLOCK_SIZE))*
		FILE_BLOCK_SIZE/DISK_SECTOR_SIZE;
	if (stream->disk_sector > disk_size(stream->disk)) {
		NOT_IMPLEMENTED();
	}
	disk_read(stream->disk, stream->disk_sector,
			FILE_BLOCK_SIZE/DISK_SECTOR_SIZE, stream->sector);
	stream->pos = offset % FILE_BLOCK_SIZE;
	stream->sector_in_memory = true;
	ASSERT(ftello(stream) == offset);
	return 0;
}

off_t
ftello(struct FILE *stream)
{
	return stream->disk_sector*DISK_SECTOR_SIZE + stream->pos;
}

void
file_check(FILE *stream)
{
  if (!stream) {
    return;
  }
  if (stream->disk) {
    disk_check(stream->disk);
  }
}

/* Prints SIZE, which represents a number of bytes, in a
   human-readable format, e.g. "256 kB". */
void
print_human_readable_size (uint64_t size)
{
  if (size == 1) {
    printf ("1 byte");
  } else {
    static const char *factors[] = {"bytes", "kB", "MB", "GB", "TB", NULL};
    const char **fp;

    for (fp = factors; size >= 1024 && fp[1] != NULL; fp++)
      size /= 1024;
    printf ("%"PRIu64" %s", size, *fp);
  }
}


#if 0
// API
int vscanf(const char *format,va_list arg_ptr){
  return vfscanf(stdin,format,arg_ptr);
}


#endif
