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

/*
 * Verify that curl_global_init_mem() refuses to install custom memory
 * callbacks after curl_getenv() already returned memory allocated with the
 * default ones, instead of silently letting curl_free() later release that
 * memory with a mismatched custom free callback. Also verify the inverse
 * direction: once a custom allocator is installed and used, a plain
 * curl_global_init() must not silently swap the allocator back to the
 * defaults while a custom-allocated block is still outstanding.
 */

static int t3150_seen;

static void *t3150_custom_calloc(size_t nmemb, size_t size)
{
  t3150_seen++;
  /* !checksrc! disable BANNEDFUNC 1 */
  return calloc(nmemb, size);
}

static void *t3150_custom_malloc(size_t size)
{
  t3150_seen++;
  /* !checksrc! disable BANNEDFUNC 1 */
  return malloc(size);
}

static char *t3150_custom_strdup(const char *ptr)
{
  t3150_seen++;
  return CURLX_STRDUP_LOW(ptr);
}

static void *t3150_custom_realloc(void *ptr, size_t size)
{
  t3150_seen++;
  /* !checksrc! disable BANNEDFUNC 1 */
  return realloc(ptr, size);
}

static void t3150_custom_free(void *ptr)
{
  t3150_seen++;
  /* !checksrc! disable BANNEDFUNC 1 */
  free(ptr);
}

static CURLcode test_lib3150(const char *URL)
{
  char *env;
  CURLcode result;
  (void)URL;

  /* this allocates with the default (not yet possibly replaced) memory
     functions, before curl_global_init()/curl_global_init_mem() is ever
     called */
  env = curl_getenv("PATH");
  if(!env) {
    curl_mfprintf(stderr, "curl_getenv(\"PATH\") returned NULL\n");
    return TEST_ERR_MAJOR_BAD;
  }

  result = curl_global_init_mem(CURL_GLOBAL_ALL,
                                t3150_custom_malloc,
                                t3150_custom_free,
                                t3150_custom_realloc,
                                t3150_custom_strdup,
                                t3150_custom_calloc);
  if(result != CURLE_FAILED_INIT) {
    curl_mfprintf(stderr, "curl_global_init_mem() returned %d, wanted %d\n",
                  (int)result, (int)CURLE_FAILED_INIT);
    curl_free(env);
    if(!result)
      curl_global_cleanup();
    return TEST_ERR_FAILURE;
  }

  if(t3150_seen) {
    curl_mfprintf(stderr, "custom callbacks were invoked %d times\n",
                  t3150_seen);
    curl_free(env);
    return TEST_ERR_FAILURE;
  }

  /* this must still be safe to release with the original allocator */
  curl_free(env);

  result = curl_global_init(CURL_GLOBAL_ALL);
  if(result) {
    curl_mfprintf(stderr, "curl_global_init() failed\n");
    return TEST_ERR_MAJOR_BAD;
  }

  curl_global_cleanup();

  /* inverse direction: a custom allocation outstanding across
     curl_global_cleanup() must prevent a later plain curl_global_init()
     from swapping the allocator back to the defaults */
  result = curl_global_init_mem(CURL_GLOBAL_ALL,
                                t3150_custom_malloc,
                                t3150_custom_free,
                                t3150_custom_realloc,
                                t3150_custom_strdup,
                                t3150_custom_calloc);
  if(result) {
    curl_mfprintf(stderr, "curl_global_init_mem() failed (2nd)\n");
    return TEST_ERR_MAJOR_BAD;
  }

  env = curl_getenv("PATH");
  if(!env) {
    curl_mfprintf(stderr, "curl_getenv(\"PATH\") returned NULL (2nd)\n");
    curl_global_cleanup();
    return TEST_ERR_MAJOR_BAD;
  }

  curl_global_cleanup();

  result = curl_global_init(CURL_GLOBAL_ALL);
  if(result != CURLE_FAILED_INIT) {
    curl_mfprintf(stderr, "curl_global_init() returned %d after cleanup "
                  "with an outstanding custom allocation, wanted %d\n",
                  (int)result, (int)CURLE_FAILED_INIT);
    curl_free(env);
    if(!result)
      curl_global_cleanup();
    return TEST_ERR_FAILURE;
  }

  /* still safe to release with the allocator that created it */
  curl_free(env);

  /* nothing outstanding now, so a plain init must succeed again */
  result = curl_global_init(CURL_GLOBAL_ALL);
  if(result) {
    curl_mfprintf(stderr, "curl_global_init() failed after the "
                  "outstanding allocation was freed\n");
    return TEST_ERR_MAJOR_BAD;
  }

  curl_global_cleanup();

  /* correct usage: freeing the custom allocation before cleanup/reinit
     must keep working */
  result = curl_global_init_mem(CURL_GLOBAL_ALL,
                                t3150_custom_malloc,
                                t3150_custom_free,
                                t3150_custom_realloc,
                                t3150_custom_strdup,
                                t3150_custom_calloc);
  if(result) {
    curl_mfprintf(stderr, "curl_global_init_mem() failed (3rd)\n");
    return TEST_ERR_MAJOR_BAD;
  }

  env = curl_getenv("PATH");
  if(!env) {
    curl_mfprintf(stderr, "curl_getenv(\"PATH\") returned NULL (3rd)\n");
    curl_global_cleanup();
    return TEST_ERR_MAJOR_BAD;
  }

  curl_free(env); /* freed while the custom allocator is still active */
  curl_global_cleanup();

  result = curl_global_init(CURL_GLOBAL_ALL);
  if(result) {
    curl_mfprintf(stderr, "curl_global_init() failed after correct "
                  "free-before-cleanup usage\n");
    return TEST_ERR_MAJOR_BAD;
  }

  curl_global_cleanup();

  return CURLE_OK;
}
