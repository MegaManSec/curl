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
#include "curl_share.h"

#if defined(USE_SSL) && defined(USE_SSLS_EXPORT)
#include "vtls/vtls_scache.h"
#endif

static CURLcode test_unit3309(const char *arg)
{
  UNITTEST_BEGIN_SIMPLE

#if defined(USE_SSL) && defined(USE_SSLS_EXPORT)
  struct Curl_easy *easy = NULL;
  CURLSH *share = NULL;
  CURLcode result;

  curl_global_init(CURL_GLOBAL_ALL);
  easy = curl_easy_init();
  if(!easy) {
    curl_global_cleanup();
    goto unit_test_abort; /* OOM during setup, not a test failure */
  }
  share = curl_share_init();
  if(!share) {
    curl_easy_cleanup(easy);
    curl_global_cleanup();
    goto unit_test_abort; /* OOM during setup, not a test failure */
  }
  fail_unless(curl_share_setopt(share, CURLSHOPT_SHARE,
                                CURL_LOCK_DATA_SSL_SESSION) == CURLSHE_OK,
              "share setopt SSL_SESSION failed");
  fail_unless(curl_easy_setopt(easy, CURLOPT_SHARE, share) == CURLE_OK,
              "easy setopt SHARE failed");

  /* Simulate another thread already legitimately holding the lock on
   * this shared scache. */
  Curl_ssl_scache_lock(easy);
  fail_unless(Curl_ssl_scache_is_locked_by_current_thread(easy),
              "scache should be locked before the import call");

  /* Malformed on purpose: neither a peer key nor a valid shmac, so
   * Curl_ssl_session_import() must fail before it ever takes its own
   * lock on the cache. */
  result = Curl_ssl_session_import(easy, NULL, NULL, 0, "x", 1);
  fail_unless(result == CURLE_BAD_FUNCTION_ARGUMENT,
              "malformed import should be rejected");

  fail_unless(Curl_ssl_scache_is_locked_by_current_thread(easy),
              "import must not drop a lock it never acquired");

  Curl_ssl_scache_unlock(easy);

  curl_easy_cleanup(easy);
  curl_share_cleanup(share);
  curl_global_cleanup();
#endif

  UNITTEST_END_SIMPLE
}
