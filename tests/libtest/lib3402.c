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

struct t3402_buf {
  size_t len;
};

static CURL *t3402_easy;
static struct t3402_buf t3402_buf1;
static struct t3402_buf t3402_buf2;
static size_t t3402_buf1_at_swap;
static int t3402_swapped;

static size_t t3402_write_cb(char *ptr, size_t size, size_t nmemb,
                             void *userp)
{
  struct t3402_buf *b = (struct t3402_buf *)userp;
  size_t len = size * nmemb;
  (void)ptr;

  b->len += len;

  if(!t3402_swapped && len == CURL_MAX_WRITE_SIZE) {
    t3402_swapped = 1;
    t3402_buf1_at_swap = b->len;
    curl_easy_setopt(t3402_easy, CURLOPT_WRITEDATA, &t3402_buf2);
  }
  return len;
}

static CURLcode test_lib3402(const char *URL)
{
  CURLcode result = CURLE_OK;

  start_test_timing();

  memset(&t3402_buf1, 0, sizeof(t3402_buf1));
  memset(&t3402_buf2, 0, sizeof(t3402_buf2));
  t3402_swapped = 0;

  global_init(CURL_GLOBAL_ALL);

  easy_init(t3402_easy);

  easy_setopt(t3402_easy, CURLOPT_URL, URL);
  easy_setopt(t3402_easy, CURLOPT_HEADER, 1L);
  easy_setopt(t3402_easy, CURLOPT_WRITEFUNCTION, t3402_write_cb);
  easy_setopt(t3402_easy, CURLOPT_WRITEDATA, &t3402_buf1);

  result = curl_easy_perform(t3402_easy);
  if(result)
    goto test_cleanup;

  if(!t3402_swapped) {
    curl_mfprintf(stderr, "t3402: callback never swapped WRITEDATA\n");
    result = TEST_ERR_FAILURE;
    goto test_cleanup;
  }
  if(t3402_buf1.len != t3402_buf1_at_swap) {
    curl_mfprintf(stderr, "t3402: first destination kept growing after the "
                  "swap: %lu bytes at swap, %lu bytes at the end\n",
                  (unsigned long)t3402_buf1_at_swap,
                  (unsigned long)t3402_buf1.len);
    result = TEST_ERR_FAILURE;
    goto test_cleanup;
  }
  if(!t3402_buf2.len) {
    curl_mfprintf(stderr, "t3402: second destination got no data, "
                  "stale WRITEDATA was used after the swap\n");
    result = TEST_ERR_FAILURE;
    goto test_cleanup;
  }

test_cleanup:

  curl_easy_cleanup(t3402_easy);
  curl_global_cleanup();

  return result;
}
