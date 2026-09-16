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

/* Curl_cpool_upkeep() and Curl_cpool_get_conn() scan the pool the same
 * way Curl_cpool_find() and Curl_cpool_prune_dead() do: they fetch the
 * next bundle node before touching the current one. A callback invoked
 * during that scan (e.g. via CURLOPT_CLOSESOCKETFUNCTION reentering
 * libcurl through a different easy or multi handle sharing the same
 * pool) can start its own scan of the same bundle. Without a guard
 * against re-entering a pool that is already locked further up the
 * call stack, that nested scan would proceed to CPOOL_LOCK/CPOOL_UNLOCK
 * again, tripping the "not already locked" debug assertion and, in a
 * non-debug build, clearing cpool->locked out from under the outer
 * scan while it is still iterating.
 */
static CURLcode t3110_setup(void)
{
  CURLcode result = CURLE_OK;
  global_init(CURL_GLOBAL_ALL);
  return result;
}

static CURLcode t3110_test(void)
{
  CURLM *multi = NULL;
  CURL *easy = NULL;
  struct Curl_easy *data;
  struct cpool *cpool;
  struct connectdata *conn;
  CURLcode result;
  CURLcode ret = CURLE_OK;

  multi = curl_multi_init();
  if(!multi) {
    fail("multi handle creation failed");
    return CURLE_FAILED_INIT;
  }
  easy = curl_easy_init();
  if(!easy) {
    fail("easy handle creation failed");
    curl_multi_cleanup(multi);
    return CURLE_FAILED_INIT;
  }
  if(curl_multi_add_handle(multi, easy) != CURLM_OK) {
    fail("adding easy handle to multi failed");
    curl_easy_cleanup(easy);
    curl_multi_cleanup(multi);
    return CURLE_FAILED_INIT;
  }

  data = easy;
  cpool = Curl_cpool_get_instance(data);
  if(!cpool) {
    fail("no connection pool for the easy handle");
    ret = CURLE_FAILED_INIT;
    goto cleanup;
  }

  fail_unless(!cpool->locked, "pool unexpectedly locked at start");

  /* simulate an outer scan that is still in progress further up the
   * call stack, e.g. Curl_cpool_find() or Curl_cpool_upkeep() itself
   * with a prefetched bundle node it has not dereferenced yet */
  cpool->locked = TRUE;

  result = Curl_cpool_upkeep(data);
  fail_unless(result == CURLE_OK, "Curl_cpool_upkeep return code");
  fail_unless(cpool->locked,
              "Curl_cpool_upkeep must not touch the lock when reentrant");

  conn = Curl_cpool_get_conn(data, 0);
  fail_unless(!conn, "Curl_cpool_get_conn must not find a connection");
  fail_unless(cpool->locked,
              "Curl_cpool_get_conn must not touch the lock when reentrant");

  cpool->locked = FALSE;

cleanup:
  curl_multi_remove_handle(multi, easy);
  curl_easy_cleanup(easy);
  curl_multi_cleanup(multi);
  return ret;
}

static CURLcode test_unit3110(const char *arg)
{
  UNITTEST_BEGIN(t3110_setup())

  if(t3110_test())
    fail("t3110_test failed");

  UNITTEST_END(curl_global_cleanup())
}
