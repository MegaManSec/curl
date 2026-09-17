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
/*
 * Verify that two easy handles in the same multi handle, sharing the same
 * cacheable CAfile criteria, do not end up with the same WOLFSSL_X509_STORE
 * when both set CURLOPT_SSL_CTX_FUNCTION. Each such handle must get its own
 * independent store so that changes made to it from the callback cannot
 * leak into other transfers using the shared CA cache.
 */
#include "first.h"

#ifdef USE_WOLFSSL
#include <wolfssl/options.h>
#include <wolfssl/version.h>
#include <wolfssl/ssl.h>

struct t3596_rec {
  WOLFSSL_X509_STORE *store;
  int called;
};

static CURLcode t3596_ssl_ctx_cb(CURL *curl, void *sslctx, void *clientp)
{
  struct t3596_rec *rec = clientp;
  (void)curl;
  rec->store = wolfSSL_CTX_get_cert_store((WOLFSSL_CTX *)sslctx);
  rec->called = 1;
  return CURLE_OK;
}

static CURL *t3596_handle(const char *url, const char *cafile,
                          struct t3596_rec *rec)
{
  CURL *curl = NULL;
  CURLcode result = CURLE_OK;

  easy_init(curl);
  easy_setopt(curl, CURLOPT_URL, url);
  easy_setopt(curl, CURLOPT_CAINFO, cafile);
  easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  easy_setopt(curl, CURLOPT_SSL_CTX_FUNCTION, t3596_ssl_ctx_cb);
  easy_setopt(curl, CURLOPT_SSL_CTX_DATA, rec);
  easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1L);
  easy_setopt(curl, CURLOPT_VERBOSE, 1L);
  return curl;

test_cleanup:
  if(curl)
    curl_easy_cleanup(curl);
  return NULL;
}

static CURLcode t3596_run(CURLM *multi, CURL *curl)
{
  CURLcode result = CURLE_OK;
  int still_running = 0;

  multi_add_handle(multi, curl);

  multi_perform(multi, &still_running);
  abort_on_test_timeout();

  while(still_running) {
    int num;
    CURLMcode mresult = curl_multi_wait(multi, NULL, 0, 1000, &num);
    if(mresult != CURLM_OK) {
      curl_mfprintf(stderr, "curl_multi_wait() returned %d\n", mresult);
      result = TEST_ERR_MULTI;
      goto test_cleanup;
    }
    abort_on_test_timeout();
    multi_perform(multi, &still_running);
    abort_on_test_timeout();
  }

test_cleanup:
  curl_multi_remove_handle(multi, curl);
  return result;
}

static CURLcode test_lib3596(const char *URL)
{
  CURL *curl1 = NULL, *curl2 = NULL;
  CURLM *multi = NULL;
  struct t3596_rec rec1, rec2;
  CURLcode result = CURLE_OK;

  memset(&rec1, 0, sizeof(rec1));
  memset(&rec2, 0, sizeof(rec2));

  start_test_timing();

  if(curl_global_sslset(CURLSSLBACKEND_WOLFSSL, NULL, NULL) != CURLSSLSET_OK) {
    curl_mfprintf(stderr, "could not set wolfSSL as backend\n");
    return TEST_ERR_FAILURE;
  }

  global_init(CURL_GLOBAL_ALL);

  multi_init(multi);

  curl1 = t3596_handle(URL, libtest_arg2, &rec1);
  if(!curl1) {
    result = TEST_ERR_EASY_INIT;
    goto test_cleanup;
  }

  /* Run the first transfer to completion before starting the second, so
     the second is guaranteed to see whatever the first one cached. Since
     the transfer succeeds, the connection stays alive in the pool and its
     WOLFSSL_X509_STORE cannot get freed and replaced by an unrelated one
     at the same address before the comparison below. */
  result = t3596_run(multi, curl1);
  if(result)
    goto test_cleanup;

  curl2 = t3596_handle(URL, libtest_arg2, &rec2);
  if(!curl2) {
    result = TEST_ERR_EASY_INIT;
    goto test_cleanup;
  }

  result = t3596_run(multi, curl2);
  if(result)
    goto test_cleanup;

  if(!rec1.called || !rec2.called) {
    curl_mfprintf(stderr, "SSL_CTX_FUNCTION callback did not fire for "
                  "both handles\n");
    result = TEST_ERR_FAILURE;
    goto test_cleanup;
  }

  if(rec1.store == rec2.store) {
    curl_mfprintf(stderr, "FAIL: both handles share the same "
                  "WOLFSSL_X509_STORE while both set "
                  "CURLOPT_SSL_CTX_FUNCTION\n");
    result = TEST_ERR_FAILURE;
    goto test_cleanup;
  }

test_cleanup:
  if(curl1)
    curl_easy_cleanup(curl1);
  if(curl2)
    curl_easy_cleanup(curl2);
  curl_multi_cleanup(multi);
  curl_global_cleanup();

  return result;
}

#else /* USE_WOLFSSL */
static CURLcode test_lib3596(const char *URL)
{
  (void)URL;
  return CURLE_OK;
}
#endif
