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

/* Unit tests for Curl_ssl_scache_use(): a transfer that installs a
 * CURLOPT_SSL_CTX_FUNCTION callback must never use the TLS session cache,
 * since libcurl cannot know what client identity, if any, the callback
 * installs into the backend SSL context. */

#include "unitcheck.h"
#include "urldata.h"
#include "cfilters.h"

#ifdef USE_SSL
#include "vtls/vtls_scache.h"

static CURLcode unit3307_ctx_cb(CURL *curl, void *ssl_ctx, void *userptr)
{
  (void)curl;
  (void)ssl_ctx;
  (void)userptr;
  return CURLE_OK;
}

static const struct Curl_cftype unit3307_cft;
#endif

static CURLcode test_unit3307(const char *arg)
{
  UNITTEST_BEGIN_SIMPLE

#ifdef USE_SSL
  struct Curl_easy *easy = curl_easy_init();
  struct connectdata conn;
  struct Curl_cfilter cf;
  CURLSH *share = curl_share_init();

  abort_if(!easy || !share, "setup failed");

  fail_unless(curl_share_setopt(share, CURLSHOPT_SHARE,
                                CURL_LOCK_DATA_SSL_SESSION) == CURLSHE_OK,
              "curl_share_setopt failed");
  fail_unless(curl_easy_setopt(easy, CURLOPT_SHARE, share) == CURLE_OK,
              "curl_easy_setopt(CURLOPT_SHARE) failed");

  memset(&conn, 0, sizeof(conn));
  conn.ssl_config.cache_session = TRUE;
  memset(&cf, 0, sizeof(cf));
  cf.cft = &unit3307_cft;
  cf.conn = &conn;

  fail_unless(Curl_ssl_scache_use(&cf, easy),
              "scache should be used with a session cache and "
              "cache_session enabled");

  easy->set.ssl_fsslctx = unit3307_ctx_cb;
  fail_unless(!Curl_ssl_scache_use(&cf, easy),
              "scache must not be used when SSL_CTX_FUNCTION is set");

  curl_easy_setopt(easy, CURLOPT_SHARE, NULL);
  curl_share_cleanup(share);
  curl_easy_cleanup(easy);
#endif /* USE_SSL */

  UNITTEST_END_SIMPLE
}
