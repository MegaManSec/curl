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
#include "urldata.h"
#include "conncache.h"

static CURLcode t1729_setup(struct Curl_easy **easy)
{
  CURLcode result = CURLE_OK;
  CURLM *multi;

  global_init(CURL_GLOBAL_ALL);
  multi = curl_multi_init();
  *easy = curl_easy_init();
  if(!multi || !*easy) {
    curl_multi_cleanup(multi);
    curl_easy_cleanup(*easy);
    curl_global_cleanup();
    return CURLE_OUT_OF_MEMORY;
  }
  curl_multi_add_handle(multi, *easy);
  return result;
}

static void t1729_stop(struct Curl_easy *easy)
{
  CURLM *multi = easy->multi;
  curl_multi_remove_handle(multi, easy);
  curl_easy_cleanup(easy);
  curl_multi_cleanup(multi);
  curl_global_cleanup();
}

static CURLcode test_unit1729(const char *arg)
{
  struct Curl_easy *easy;

  UNITTEST_BEGIN(t1729_setup(&easy))

  struct cpool cpool;
  struct connectdata conn;

  memset(&cpool, 0, sizeof(cpool));
  memset(&conn, 0, sizeof(conn));
  conn.attached_xfers = 1;

  cpool_unit_discard_conn(&cpool, easy, &conn, TRUE);

  fail_if(conn.bits.aborted,
          "an in-use connection must not be marked aborted and torn "
          "down, even when the caller requested an aborted discard");
  fail_unless(conn.attached_xfers == 1,
              "an in-use connection must be left untouched");

  UNITTEST_END(t1729_stop(easy))
}
