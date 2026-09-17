/***************************************************************************
 *                                  _   _ ____  _
 *  Project                     ___| | | |  _ \| |
 *                             / __| | | | |_) | |
 *                            | (__| |_| |  _ <| |___
 *                             \___|\___/|_| \_\_____|
 *
 * Copyright (C) Daniel Stenberg, <daniel@haxx.se>, et al.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution. The terms
 * are also available at https://curl.se/docs/copyright.html.
 *
 * You may opt to use, copy, modify, merge, publish, distribute and/or sell
 * copies of the Software, and permit persons to whom the Software is
 * furnished to do so, under the terms of the COPYING file.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 * SPDX-License-Identifier: curl
 *
 ***************************************************************************/
#include "tool_setup.h"

#include "tool_cfgable.h"
#include "tool_writeout_json.h"
#include "tool_writeout.h"

#define MAX_JSON_STRING 100000

/* appended within the quotes when a value is cut off at MAX_JSON_STRING, so
   the output stays valid JSON instead of silently omitting the value */
#define JSON_TRUNC_SUFFIX "..."
#define JSON_TRUNC_SUFFIX_LEN (sizeof(JSON_TRUNC_SUFFIX) - 1)

/* Return the length of the valid UTF-8 sequence starting at 'p', with
   'left' bytes available to read. Returns 0 if there is no valid UTF-8
   sequence starting at 'p'. */
static size_t utf8seqlen(const unsigned char *p, size_t left)
{
  size_t need;
  unsigned char lo;
  unsigned char hi;
  size_t n;

  if(*p < 0x80)
    return 1;
  else if(*p >= 0xc2 && *p <= 0xdf) {
    need = 2;
    lo = 0x80;
    hi = 0xbf;
  }
  else if(*p == 0xe0) {
    need = 3;
    lo = 0xa0;
    hi = 0xbf;
  }
  else if(*p == 0xed) {
    need = 3;
    lo = 0x80;
    hi = 0x9f;
  }
  else if(*p >= 0xe1 && *p <= 0xef) {
    need = 3;
    lo = 0x80;
    hi = 0xbf;
  }
  else if(*p == 0xf0) {
    need = 4;
    lo = 0x90;
    hi = 0xbf;
  }
  else if(*p == 0xf4) {
    need = 4;
    lo = 0x80;
    hi = 0x8f;
  }
  else if(*p >= 0xf1 && *p <= 0xf3) {
    need = 4;
    lo = 0x80;
    hi = 0xbf;
  }
  else
    return 0;

  if(left < need || p[1] < lo || p[1] > hi)
    return 0;
  for(n = 2; n < need; n++)
    if(p[n] < 0x80 || p[n] > 0xbf)
      return 0;
  return need;
}

/* provide the given string in dynbuf as a quoted json string, but without the
   outer quotes. The buffer is not inited by this function.

   If the escaped output would exceed MAX_JSON_STRING, it is cut off and
   JSON_TRUNC_SUFFIX is appended so the result remains valid JSON.

   Return 0 on success, non-zero on error. */
int jsonquoted(const char *in, size_t len, struct dynbuf *out, bool lowercase)
{
  const unsigned char *i = (const unsigned char *)in;
  const unsigned char *in_end = &i[len];
  CURLcode result = CURLE_OK;
  size_t avail = MAX_JSON_STRING - 1 - JSON_TRUNC_SUFFIX_LEN;
  bool trunc = FALSE;

  for(; (i < in_end) && !result; i++) {
    size_t needed;
    switch(*i) {
    case '\\':
    case '\"':
    case '\b':
    case '\f':
    case '\n':
    case '\r':
    case '\t':
      needed = 2;
      break;
    default:
      needed = (*i < 32) ? 6 : 1;
      break;
    }
    if(curlx_dyn_len(out) + needed > avail) {
      trunc = TRUE;
      break;
    }
    switch(*i) {
    case '\\':
      result = curlx_dyn_addn(out, "\\\\", 2);
      break;
    case '\"':
      result = curlx_dyn_addn(out, "\\\"", 2);
      break;
    case '\b':
      result = curlx_dyn_addn(out, "\\b", 2);
      break;
    case '\f':
      result = curlx_dyn_addn(out, "\\f", 2);
      break;
    case '\n':
      result = curlx_dyn_addn(out, "\\n", 2);
      break;
    case '\r':
      result = curlx_dyn_addn(out, "\\r", 2);
      break;
    case '\t':
      result = curlx_dyn_addn(out, "\\t", 2);
      break;
    default:
      if(*i < 32)
        result = curlx_dyn_addf(out, "\\u%04x", *i);
      else if(*i < 0x80) {
        char o = (char)*i;
        if(lowercase && (o >= 'A' && o <= 'Z'))
          /* do not use tolower() since that is locale specific */
          o |= ('a' - 'A');
        result = curlx_dyn_addn(out, &o, 1);
      }
      else {
        size_t seqlen = utf8seqlen(i, (size_t)(in_end - i));
        if(seqlen)
          result = curlx_dyn_addn(out, (const char *)i, seqlen);
        else {
          seqlen = 1;
          /* invalid UTF-8, replace with U+FFFD */
          result = curlx_dyn_addn(out, "\xef\xbf\xbd", 3);
        }
        i += seqlen - 1;
      }
      break;
    }
  }
  if(!result && trunc)
    result = curlx_dyn_addn(out, JSON_TRUNC_SUFFIX, JSON_TRUNC_SUFFIX_LEN);
  if(result)
    return (int)result;
  return 0;
}

void jsonWriteString(FILE *stream, const char *in, bool lowercase)
{
  struct dynbuf out;
  curlx_dyn_init(&out, MAX_JSON_STRING);

  if(!jsonquoted(in, strlen(in), &out, lowercase)) {
    fputc('\"', stream);
    if(curlx_dyn_len(&out))
      fputs(curlx_dyn_ptr(&out), stream);
    fputc('\"', stream);
  }
  curlx_dyn_free(&out);
}

void ourWriteOutJSON(FILE *stream, const struct writeoutvar mappings[],
                     size_t nentries,
                     struct per_transfer *per, CURLcode per_result)
{
  size_t i;

  fputs("{", stream);

  for(i = 0; i < nentries; i++) {
    if(mappings[i].writefunc &&
       mappings[i].writefunc(stream, &mappings[i], per, per_result, TRUE))
      fputs(",", stream);
  }

  /* The variables are sorted in alphabetical order but as a special case
     curl_version (which is not actually a --write-out variable) is last. */
  curl_mfprintf(stream, "\"curl_version\":");
  jsonWriteString(stream, curl_version(), FALSE);
  curl_mfprintf(stream, "}");
}

void headerJSON(FILE *stream, struct per_transfer *per)
{
  struct curl_header *header;
  struct curl_header *prev = NULL;

  fputc('{', stream);
  while((header = curl_easy_nextheader(per->curl, CURLH_HEADER, -1,
                                       prev)) != NULL) {
    if(header->amount > 1) {
      if(!header->index) {
        /* act on the 0-index entry and pull the others in, then output in a
           JSON list */
        size_t a = header->amount;
        size_t i = 0;
        char *name = header->name;
        if(prev)
          fputs(",\n", stream);
        jsonWriteString(stream, header->name, TRUE);
        fputc(':', stream);
        prev = header;
        fputc('[', stream);
        do {
          jsonWriteString(stream, header->value, FALSE);
          if(++i >= a)
            break;
          fputc(',', stream);
          if(curl_easy_header(per->curl, name, i, CURLH_HEADER, -1, &header))
            break;
        } while(1);
        fputc(']', stream);
      }
    }
    else {
      if(prev)
        fputs(",\n", stream);
      jsonWriteString(stream, header->name, TRUE);
      fputc(':', stream);
      fputc('[', stream);
      jsonWriteString(stream, header->value, FALSE);
      fputc(']', stream);
      prev = header;
    }
  }
  fputs("\n}", stream);
}
