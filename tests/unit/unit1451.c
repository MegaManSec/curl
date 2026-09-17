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

#if !defined(_WIN32) && \
  (!defined(CURL_DISABLE_COOKIES) || !defined(CURL_DISABLE_ALTSVC) || \
   !defined(CURL_DISABLE_HSTS))

#include "curl_fopen.h"

#define T1451_SENTINEL "SECRET-PROTECTED-DATA-DO-NOT-TRUNCATE\n"

static CURLcode t1451_setup(struct Curl_easy **easy)
{
  CURLcode result = CURLE_OK;

  global_init(CURL_GLOBAL_ALL);
  *easy = curl_easy_init();
  if(!*easy) {
    curl_global_cleanup();
    return CURLE_OUT_OF_MEMORY;
  }
  return result;
}

static void t1451_stop(struct Curl_easy *easy)
{
  curl_easy_cleanup(easy);
  curl_global_cleanup();
}

static CURLcode t1451_write_sentinel(const char *path)
{
  FILE *fp = curlx_fopen(path, FOPEN_WRITETEXT);
  if(!fp)
    return CURLE_WRITE_ERROR;
  fputs(T1451_SENTINEL, fp);
  curlx_fclose(fp);
  return CURLE_OK;
}

static bool t1451_has_sentinel(const char *path)
{
  char buf[sizeof(T1451_SENTINEL) + 16];
  size_t n;
  FILE *fp = curlx_fopen(path, FOPEN_READTEXT);
  if(!fp)
    return FALSE;
  n = fread(buf, 1, sizeof(buf) - 1, fp);
  curlx_fclose(fp);
  buf[n] = '\0';
  return !strcmp(buf, T1451_SENTINEL);
}

static CURLcode test_unit1451(const char *arg)
{
  struct Curl_easy *data;
  char *victim = NULL;
  char *jarlink = NULL;
  FILE *out = NULL;
  char *tempname = NULL;
  CURLcode result;

  UNITTEST_BEGIN(t1451_setup(&data))

  victim = curl_maprintf("%s-victim", arg);
  jarlink = curl_maprintf("%s-jar", arg);
  abort_if(!victim || !jarlink, "out of memory");

  unlink(jarlink);
  unlink(victim);

  abort_if(t1451_write_sentinel(victim), "could not create victim file");
  abort_if(symlink(victim, jarlink) == -1, "could not create symlink");

  result = Curl_fopen(data, jarlink, &out, &tempname);
  fail_unless(result == CURLE_OK, "Curl_fopen failed on symlinked target");
  fail_unless(!!tempname, "Curl_fopen skipped the safe temp file dance");
  fail_unless(t1451_has_sentinel(victim),
              "symlink target got truncated before the temp file rename");

  if(out) {
    fputs("this is the new persisted content\n", out);
    curlx_fclose(out);
  }

  if(tempname) {
    fail_unless(curlx_rename(tempname, jarlink) == 0,
                "failed to rename the temp file over the symlink");
    curlx_free(tempname);
  }

  fail_unless(t1451_has_sentinel(victim),
              "symlink target was modified by the save");

  {
    struct stat sb;
    fail_unless(lstat(jarlink, &sb) != -1 && S_ISREG(sb.st_mode),
                "the save path should now be a regular file, not a symlink");
  }

  unlink(jarlink);
  unlink(victim);
  curlx_free(victim);
  curlx_free(jarlink);

  UNITTEST_END(t1451_stop(data))
}

#else
static CURLcode test_unit1451(const char *arg)
{
  UNITTEST_BEGIN_SIMPLE
  UNITTEST_END_SIMPLE
}
#endif
