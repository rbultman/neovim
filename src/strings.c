#include <string.h>

#include "vim.h"
#include "strings.h"
#include "misc2.h"
#include "file_search.h"
#include "blowfish.h"
#include "buffer.h"
#include "charset.h"
#include "diff.h"
#include "edit.h"
#include "eval.h"
#include "ex_cmds.h"
#include "ex_docmd.h"
#include "ex_getln.h"
#include "fileio.h"
#include "fold.h"
#include "getchar.h"
#include "mark.h"
#include "mbyte.h"
#include "memfile.h"
#include "memline.h"
#include "memory.h"
#include "message.h"
#include "misc1.h"
#include "move.h"
#include "option.h"
#include "ops.h"
#include "os_unix.h"
#include "path.h"
#include "quickfix.h"
#include "regexp.h"
#include "screen.h"
#include "search.h"
#include "spell.h"
#include "syntax.h"
#include "tag.h"
#include "term.h"
#include "ui.h"
#include "window.h"
#include "os/os.h"
#include "os/shell.h"

/*
 * Copy "string" into newly allocated memory.
 */
char_u *vim_strsave(char_u *string)
{
  char_u      *p;
  unsigned len;

  len = (unsigned)STRLEN(string) + 1;
  p = alloc(len);
  if (p != NULL)
    memmove(p, string, (size_t)len);
  return p;
}

/*
 * Copy up to "len" bytes of "string" into newly allocated memory and
 * terminate with a NUL.
 * The allocated memory always has size "len + 1", also when "string" is
 * shorter.
 */
char_u *vim_strnsave(char_u *string, int len)
{
  char_u      *p;

  p = alloc((unsigned)(len + 1));
  STRNCPY(p, string, len);
  p[len] = NUL;
  return p;
}

/*
 * Same as vim_strsave(), but any characters found in esc_chars are preceded
 * by a backslash.
 */
char_u *vim_strsave_escaped(char_u *string, char_u *esc_chars)
{
  return vim_strsave_escaped_ext(string, esc_chars, '\\', FALSE);
}

/*
 * Same as vim_strsave_escaped(), but when "bsl" is TRUE also escape
 * characters where rem_backslash() would remove the backslash.
 * Escape the characters with "cc".
 */
char_u *vim_strsave_escaped_ext(char_u *string, char_u *esc_chars, int cc, int bsl)
{
  char_u      *p;
  char_u      *p2;
  char_u      *escaped_string;
  unsigned length;
  int l;

  /*
   * First count the number of backslashes required.
   * Then allocate the memory and insert them.
   */
  length = 1;                           /* count the trailing NUL */
  for (p = string; *p; p++) {
    if (has_mbyte && (l = (*mb_ptr2len)(p)) > 1) {
      length += l;                      /* count a multibyte char */
      p += l - 1;
      continue;
    }
    if (vim_strchr(esc_chars, *p) != NULL || (bsl && rem_backslash(p)))
      ++length;                         /* count a backslash */
    ++length;                           /* count an ordinary char */
  }
  escaped_string = alloc(length);
  p2 = escaped_string;
  for (p = string; *p; p++) {
    if (has_mbyte && (l = (*mb_ptr2len)(p)) > 1) {
      memmove(p2, p, (size_t)l);
      p2 += l;
      p += l - 1;                     /* skip multibyte char  */
      continue;
    }
    if (vim_strchr(esc_chars, *p) != NULL || (bsl && rem_backslash(p)))
      *p2++ = cc;
    *p2++ = *p;
  }
  *p2 = NUL;

  return escaped_string;
}

/*
 * Escape "string" for use as a shell argument with system().
 * This uses single quotes, except when we know we need to use double quotes
 * (MS-Windows without 'shellslash' set).
 * Escape a newline, depending on the 'shell' option.
 * When "do_special" is true also replace "!", "%", "#" and things starting
 * with "<" like "<cfile>".
 * When "do_newline" is false do not escape newline unless it is csh shell.
 * Returns the result in allocated memory.
 */
char_u *vim_strsave_shellescape(char_u *string, bool do_special, bool do_newline)
{
  unsigned length;
  char_u      *p;
  char_u      *d;
  char_u      *escaped_string;
  int l;
  int csh_like;

  /* Only csh and similar shells expand '!' within single quotes.  For sh and
   * the like we must not put a backslash before it, it will be taken
   * literally.  If do_special is set the '!' will be escaped twice.
   * Csh also needs to have "\n" escaped twice when do_special is set. */
  csh_like = csh_like_shell();

  /* First count the number of extra bytes required. */
  length = (unsigned)STRLEN(string) + 3;    /* two quotes and a trailing NUL */
  for (p = string; *p != NUL; mb_ptr_adv(p)) {
    if (*p == '\'')
      length += 3;                      /* ' => '\'' */
    if ((*p == '\n' && (csh_like || do_newline))
        || (*p == '!' && (csh_like || do_special))) {
      ++length;                         /* insert backslash */
      if (csh_like && do_special)
        ++length;                       /* insert backslash */
    }
    if (do_special && find_cmdline_var(p, &l) >= 0) {
      ++length;                         /* insert backslash */
      p += l - 1;
    }
  }

  /* Allocate memory for the result and fill it. */
  escaped_string = alloc(length);
  d = escaped_string;

  /* add opening quote */
  *d++ = '\'';

  for (p = string; *p != NUL; ) {
    if (*p == '\'') {
      *d++ = '\'';
      *d++ = '\\';
      *d++ = '\'';
      *d++ = '\'';
      ++p;
      continue;
    }
    if ((*p == '\n' && (csh_like || do_newline))
        || (*p == '!' && (csh_like || do_special))) {
      *d++ = '\\';
      if (csh_like && do_special)
        *d++ = '\\';
      *d++ = *p++;
      continue;
    }
    if (do_special && find_cmdline_var(p, &l) >= 0) {
      *d++ = '\\';                    /* insert backslash */
      while (--l >= 0)                /* copy the var */
        *d++ = *p++;
      continue;
    }

    MB_COPY_CHAR(p, d);
  }

  /* add terminating quote and finish with a NUL */
  *d++ = '\'';
  *d = NUL;

  return escaped_string;
}

/*
 * Like vim_strsave(), but make all characters uppercase.
 * This uses ASCII lower-to-upper case translation, language independent.
 */
char_u *vim_strsave_up(char_u *string)
{
  char_u *p1;

  p1 = vim_strsave(string);
  vim_strup(p1);
  return p1;
}

/*
 * Like vim_strnsave(), but make all characters uppercase.
 * This uses ASCII lower-to-upper case translation, language independent.
 */
char_u *vim_strnsave_up(char_u *string, int len)
{
  char_u *p1;

  p1 = vim_strnsave(string, len);
  vim_strup(p1);
  return p1;
}

/*
 * ASCII lower-to-upper case translation, language independent.
 */
void vim_strup(char_u *p)
{
  char_u  *p2;
  int c;

  if (p != NULL) {
    p2 = p;
    while ((c = *p2) != NUL)
      *p2++ = (c < 'a' || c > 'z') ? c : (c - 0x20);
  }
}

/*
 * Make string "s" all upper-case and return it in allocated memory.
 * Handles multi-byte characters as well as possible.
 * Returns NULL when out of memory.
 */
char_u *strup_save(char_u *orig)
{
  char_u      *p;
  char_u      *res;

  res = p = vim_strsave(orig);

  if (res != NULL)
    while (*p != NUL) {
      int l;

      if (enc_utf8) {
        int c, uc;
        int newl;
        char_u  *s;

        c = utf_ptr2char(p);
        uc = utf_toupper(c);

        /* Reallocate string when byte count changes.  This is rare,
         * thus it's OK to do another malloc()/free(). */
        l = utf_ptr2len(p);
        newl = utf_char2len(uc);
        if (newl != l) {
          s = alloc((unsigned)STRLEN(res) + 1 + newl - l);
          memmove(s, res, p - res);
          STRCPY(s + (p - res) + newl, p + l);
          p = s + (p - res);
          vim_free(res);
          res = s;
        }

        utf_char2bytes(uc, p);
        p += newl;
      } else if (has_mbyte && (l = (*mb_ptr2len)(p)) > 1)
        p += l;                 /* skip multi-byte character */
      else {
        *p = TOUPPER_LOC(*p);         /* note that toupper() can be a macro */
        p++;
      }
    }

  return res;
}

/*
 * copy a space a number of times
 */
void copy_spaces(char_u *ptr, size_t count)
{
  size_t i = count;
  char_u      *p = ptr;

  while (i--)
    *p++ = ' ';
}

/*
 * Copy a character a number of times.
 * Does not work for multi-byte characters!
 */
void copy_chars(char_u *ptr, size_t count, int c)
{
  size_t i = count;
  char_u      *p = ptr;

  while (i--)
    *p++ = c;
}

/*
 * delete spaces at the end of a string
 */
void del_trailing_spaces(char_u *ptr)
{
  char_u      *q;

  q = ptr + STRLEN(ptr);
  while (--q > ptr && vim_iswhite(q[0]) && q[-1] != '\\' && q[-1] != Ctrl_V)
    *q = NUL;
}

/*
 * Like strncpy(), but always terminate the result with one NUL.
 * "to" must be "len + 1" long!
 */
void vim_strncpy(char_u *to, char_u *from, size_t len)
{
  STRNCPY(to, from, len);
  to[len] = NUL;
}

/*
 * Like strcat(), but make sure the result fits in "tosize" bytes and is
 * always NUL terminated.
 */
void vim_strcat(char_u *to, char_u *from, size_t tosize)
{
  size_t tolen = STRLEN(to);
  size_t fromlen = STRLEN(from);

  if (tolen + fromlen + 1 > tosize) {
    memmove(to + tolen, from, tosize - tolen - 1);
    to[tosize - 1] = NUL;
  } else
    STRCPY(to + tolen, from);
}

#if (!defined(HAVE_STRCASECMP) && !defined(HAVE_STRICMP)) || defined(PROTO)
/*
 * Compare two strings, ignoring case, using current locale.
 * Doesn't work for multi-byte characters.
 * return 0 for match, < 0 for smaller, > 0 for bigger
 */
int vim_stricmp(char *s1, char *s2)
{
  int i;

  for (;; ) {
    i = (int)TOLOWER_LOC(*s1) - (int)TOLOWER_LOC(*s2);
    if (i != 0)
      return i;                             /* this character different */
    if (*s1 == NUL)
      break;                                /* strings match until NUL */
    ++s1;
    ++s2;
  }
  return 0;                                 /* strings match */
}
#endif

#if (!defined(HAVE_STRNCASECMP) && !defined(HAVE_STRNICMP)) || defined(PROTO)
/*
 * Compare two strings, for length "len", ignoring case, using current locale.
 * Doesn't work for multi-byte characters.
 * return 0 for match, < 0 for smaller, > 0 for bigger
 */
int vim_strnicmp(char *s1, char *s2, size_t len)
{
  int i;

  while (len > 0) {
    i = (int)TOLOWER_LOC(*s1) - (int)TOLOWER_LOC(*s2);
    if (i != 0)
      return i;                             /* this character different */
    if (*s1 == NUL)
      break;                                /* strings match until NUL */
    ++s1;
    ++s2;
    --len;
  }
  return 0;                                 /* strings match */
}
#endif

/*
 * Version of strchr() and strrchr() that handle unsigned char strings
 * with characters from 128 to 255 correctly.  It also doesn't return a
 * pointer to the NUL at the end of the string.
 */
char_u *vim_strchr(char_u *string, int c)
{
  char_u      *p;
  int b;

  p = string;
  if (enc_utf8 && c >= 0x80) {
    while (*p != NUL) {
      if (utf_ptr2char(p) == c)
        return p;
      p += (*mb_ptr2len)(p);
    }
    return NULL;
  }
  if (enc_dbcs != 0 && c > 255) {
    int n2 = c & 0xff;

    c = ((unsigned)c >> 8) & 0xff;
    while ((b = *p) != NUL) {
      if (b == c && p[1] == n2)
        return p;
      p += (*mb_ptr2len)(p);
    }
    return NULL;
  }
  if (has_mbyte) {
    while ((b = *p) != NUL) {
      if (b == c)
        return p;
      p += (*mb_ptr2len)(p);
    }
    return NULL;
  }
  while ((b = *p) != NUL) {
    if (b == c)
      return p;
    ++p;
  }
  return NULL;
}

/*
 * Version of strchr() that only works for bytes and handles unsigned char
 * strings with characters above 128 correctly. It also doesn't return a
 * pointer to the NUL at the end of the string.
 */
char_u *vim_strbyte(char_u *string, int c)
{
  char_u      *p = string;

  while (*p != NUL) {
    if (*p == c)
      return p;
    ++p;
  }
  return NULL;
}

/*
 * Search for last occurrence of "c" in "string".
 * Return NULL if not found.
 * Does not handle multi-byte char for "c"!
 */
char_u *vim_strrchr(char_u *string, int c)
{
  char_u      *retval = NULL;
  char_u      *p = string;

  while (*p) {
    if (*p == c)
      retval = p;
    mb_ptr_adv(p);
  }
  return retval;
}

/*
 * Vim has its own isspace() function, because on some machines isspace()
 * can't handle characters above 128.
 */
int vim_isspace(int x)
{
  return (x >= 9 && x <= 13) || x == ' ';
}

/*
 * Sort an array of strings.
 */
static int
sort_compare(const void *s1, const void *s2);

static int sort_compare(const void *s1, const void *s2)
{
  return STRCMP(*(char **)s1, *(char **)s2);
}

void sort_strings(char_u **files, int count)
{
  qsort((void *)files, (size_t)count, sizeof(char_u *), sort_compare);
}

#if (defined(FEAT_MBYTE) && defined(FEAT_QUICKFIX)) \
  || defined(FEAT_SPELL) || defined(PROTO)
/*
 * Return TRUE if string "s" contains a non-ASCII character (128 or higher).
 * When "s" is NULL FALSE is returned.
 */
int has_non_ascii(char_u *s)
{
  char_u      *p;

  if (s != NULL)
    for (p = s; *p != NUL; ++p)
      if (*p >= 128)
        return TRUE;
  return FALSE;
}
#endif

/*
 * Concatenate two strings and return the result in allocated memory.
 * Returns NULL when out of memory.
 */
char_u *concat_str(char_u *str1, char_u *str2)
{
  size_t l = STRLEN(str1);
  char_u *dest = xmalloc(l + STRLEN(str2) + 1);
  STRCPY(dest, str1);
  STRCPY(dest + l, str2);
  return dest;
}

