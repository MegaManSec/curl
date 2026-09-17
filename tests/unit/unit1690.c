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
#include "unitcheck.h"

#if !defined(CURL_DISABLE_COOKIES) || !defined(CURL_DISABLE_ALTSVC) || \
  !defined(CURL_DISABLE_HSTS)

#include "urldata.h"
#include "curl_fopen.h"

static const char t1690_original[] = "SECRET-DATA-DO-NOT-TRUNCATE\n";
static const char t1690_updated[] = "new-content\n";

/* reads a whole file into a malloc'ed, null-terminated buffer */
static char *t1690_slurp(const char *filename)
{
  char *buf = NULL;
  size_t len = 0;
  FILE *fh = curlx_fopen(filename, FOPEN_READTEXT);
  if(!fh)
    return NULL;
  buf = curlx_malloc(256);
  if(buf)
    len = fread(buf, 1, 255, fh);
  curlx_fclose(fh);
  if(!buf)
    return NULL;
  buf[len] = '\0';
  return buf;
}

static CURLcode test_unit1690(const char *arg)
{
  UNITTEST_BEGIN_SIMPLE

  CURL *easy;
  FILE *out = NULL;
  FILE *victim;
  char *tempstore = NULL;
  char *content;

  abort_if(!arg || !*arg, "missing test file argument");

  curl_global_init(CURL_GLOBAL_ALL);
  easy = curl_easy_init();
  if(!easy) {
    curl_global_cleanup();
    abort_unless(easy, "curl_easy_init()");
  }

  /* create the pre-existing target file with known content, exactly like
     a cookiejar/HSTS/Alt-Svc cache that already holds valid data */
  victim = curlx_fopen(arg, FOPEN_WRITETEXT);
  if(!victim) {
    curl_easy_cleanup(easy);
    curl_global_cleanup();
    abort_unless(victim, "fopen(victim, w)");
  }
  fputs(t1690_original, victim);
  curlx_fclose(victim);

  /* Curl_fopen() must not touch the existing file's content before the
     temp file it creates is ready to be renamed into place */
  abort_if(Curl_fopen(easy, arg, &out, &tempstore), "Curl_fopen() failed");
  abort_unless(out, "Curl_fopen() did not set *fh");
  abort_unless(tempstore, "Curl_fopen() did not set *tempname");

  content = t1690_slurp(arg);
  abort_unless(content, "could not read back the original file");
  fail_unless(!strcmp(content, t1690_original),
              "Curl_fopen() truncated the original file early");
  curlx_free(content);

  /* simulate a failed save: abandon the temp file without ever renaming
     it into place, the same way the real callers react to a write error */
  curlx_fclose(out);
  unlink(tempstore);
  curlx_safefree(tempstore);

  content = t1690_slurp(arg);
  abort_unless(content, "could not read back the file after aborted save");
  fail_unless(!strcmp(content, t1690_original),
              "original file lost after an aborted save");
  curlx_free(content);

  /* now perform a real save: write, close, rename */
  abort_if(Curl_fopen(easy, arg, &out, &tempstore), "Curl_fopen() failed (2)");
  fputs(t1690_updated, out);
  curlx_fclose(out);
  abort_if(curlx_rename(tempstore, arg), "curlx_rename() failed");
  curlx_free(tempstore);

  content = t1690_slurp(arg);
  abort_unless(content, "could not read back the saved file");
  fail_unless(!strcmp(content, t1690_updated),
              "saved file does not contain the new content");
  curlx_free(content);

  unlink(arg);
  curl_easy_cleanup(easy);
  curl_global_cleanup();

  UNITTEST_END_SIMPLE
}
#else
static CURLcode test_unit1690(const char *arg)
{
  UNITTEST_BEGIN_SIMPLE
  puts("nothing to do when cookies, HSTS and Alt-Svc are all disabled");
  UNITTEST_END_SIMPLE
}
#endif
