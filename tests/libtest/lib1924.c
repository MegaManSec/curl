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
#include "first.h"

#if defined(HAVE_GETRLIMIT) && defined(HAVE_SETRLIMIT) && \
  defined(RLIMIT_FSIZE) && defined(SIGXFSZ)
#define T1924_ENABLE_TEST 1
#include <signal.h>
#endif

/* Verify that an HSTS cache save that fails while writing (simulated with a
   tiny RLIMIT_FSIZE) never gets renamed on top of a pre-existing, valid
   cache file, so that file is not left containing partial/truncated data. */

#define T1924_SEED "example.com \"20380101 00:00:00\"\n"

static size_t t1924_discard(char *ptr, size_t size, size_t nmemb, void *ud)
{
  (void)ptr;
  (void)ud;
  return size * nmemb;
}

static CURLcode test_lib1924(const char *URL)
{
  CURLcode result;
  CURL *curl;
  FILE *fp;
#ifdef T1924_ENABLE_TEST
  struct rlimit rl_orig;
  struct rlimit rl;
  size_t nread = 0;
  char buf[256];
#endif

  fp = curlx_fopen(libtest_arg2, FOPEN_WRITETEXT);
  if(!fp) {
    curl_mfprintf(stderr, "couldn't seed the HSTS cache file\n");
    return TEST_ERR_MAJOR_BAD;
  }
  fputs(T1924_SEED, fp);
  curlx_fclose(fp);

  result = curl_global_init(CURL_GLOBAL_ALL);
  if(result)
    return result;

  curl = curl_easy_init();
  if(!curl) {
    curl_global_cleanup();
    return TEST_ERR_MAJOR_BAD;
  }

  easy_setopt(curl, CURLOPT_HSTS_CTRL, (long)CURLHSTS_ENABLE);
  easy_setopt(curl, CURLOPT_HSTS, libtest_arg2);
  easy_setopt(curl, CURLOPT_URL, URL);
  easy_setopt(curl, CURLOPT_WRITEFUNCTION, t1924_discard);

  result = curl_easy_perform(curl);
  if(result)
    goto test_cleanup;

#ifdef T1924_ENABLE_TEST
  if(getrlimit(RLIMIT_FSIZE, &rl_orig)) {
    curl_mfprintf(stderr, "getrlimit() failed\n");
    result = TEST_ERR_MAJOR_BAD;
    goto test_cleanup;
  }
  signal(SIGXFSZ, SIG_IGN);
  rl = rl_orig;
  rl.rlim_cur = 1;
  if(setrlimit(RLIMIT_FSIZE, &rl)) {
    curl_mfprintf(stderr, "setrlimit() failed\n");
    result = TEST_ERR_MAJOR_BAD;
    goto test_cleanup;
  }
#endif

  curl_easy_cleanup(curl);
  curl = NULL;

#ifdef T1924_ENABLE_TEST
  setrlimit(RLIMIT_FSIZE, &rl_orig);

  nread = 0;
  fp = curlx_fopen(libtest_arg2, FOPEN_READTEXT);
  if(fp) {
    nread = fread(buf, 1, sizeof(buf), fp);
    curlx_fclose(fp);
  }

  if(nread) {
    curl_mfprintf(stderr, "FAIL: HSTS cache has %lu bytes of unexpected "
                  "data, expected the simulated write failure to abort "
                  "the save instead of installing truncated data over "
                  "the previously valid cache\n", (unsigned long)nread);
    result = TEST_ERR_FAILURE;
  }
  else
    curl_mprintf("HSTS cache correctly left without partial data\n");
#else
  curl_mprintf("rlimit-based fault injection unsupported here, skipping\n");
#endif

test_cleanup:
  if(curl)
    curl_easy_cleanup(curl);
  curl_global_cleanup();
  return result;
}
