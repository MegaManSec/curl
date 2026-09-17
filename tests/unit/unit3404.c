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

static CURLcode t3404_setup(struct Curl_easy **easy)
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

static void t3404_stop(struct Curl_easy *easy)
{
  curl_easy_cleanup(easy);
  curl_global_cleanup();
}

static CURLcode test_unit3404(const char *arg)
{
  struct Curl_easy *easy;
  char buf[16];
  CURLcode result;

  UNITTEST_BEGIN(t3404_setup(&easy))

  result = curl_easy_recv(easy, buf, sizeof(buf), NULL);
  fail_unless(result == CURLE_BAD_FUNCTION_ARGUMENT,
              "curl_easy_recv() with NULL n should fail");

  result = curl_easy_send(easy, buf, sizeof(buf), NULL);
  fail_unless(result == CURLE_BAD_FUNCTION_ARGUMENT,
              "curl_easy_send() with NULL n should fail");

  UNITTEST_END(t3404_stop(easy))
}
