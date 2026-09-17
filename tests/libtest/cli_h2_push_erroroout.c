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

#include "testtrace.h"

/* the push callback rejects every push and asks libcurl to fail the
   parent transfer as well */
static int push_erroroout_callback(CURL *parent,
                                   CURL *curl,
                                   size_t num_headers,
                                   struct curl_pushheaders *headers,
                                   void *userp)
{
  (void)parent;
  (void)curl;
  (void)num_headers;
  (void)headers;
  (void)userp;
  return CURL_PUSH_ERROROUT;
}

static size_t discard_write_cb(char *ptr, size_t sz, size_t nm, void *ud)
{
  (void)ptr;
  (void)ud;
  return sz * nm;
}

/*
 * Verify that CURL_PUSH_ERROROUT returned from the push callback makes
 * the parent transfer fail, instead of only cancelling the pushed stream.
 */
static CURLcode test_cli_h2_push_erroroout(const char *URL)
{
  CURL *curl = NULL;
  CURLM *multi = NULL;
  CURLcode result = CURLE_OK;
  int parent_seen = 0;
  int still_running = 1;
  CURLcode parent_result = CURLE_OK;

  if(!URL) {
    curl_mfprintf(stderr, "need URL as argument\n");
    return (CURLcode)2;
  }

  if(curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) {
    curl_mfprintf(stderr, "curl_global_init() failed\n");
    return (CURLcode)3;
  }

  multi = curl_multi_init();
  if(!multi) {
    result = (CURLcode)1;
    goto cleanup;
  }

  curl = curl_easy_init();
  if(!curl) {
    result = (CURLcode)1;
    goto cleanup;
  }

  curl_easy_setopt(curl, CURLOPT_URL, URL);
  curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_write_cb);
  curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
  curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, libtest_debug_cb);
  curl_easy_setopt(curl, CURLOPT_DEBUGDATA, &debug_config);
  curl_easy_setopt(curl, CURLOPT_PIPEWAIT, 1L);

  curl_multi_setopt(multi, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
  curl_multi_setopt(multi, CURLMOPT_PUSHFUNCTION, push_erroroout_callback);

  curl_multi_add_handle(multi, curl);

  do {
    struct CURLMsg *m;
    CURLMcode mresult = curl_multi_perform(multi, &still_running);

    if(still_running)
      mresult = curl_multi_poll(multi, NULL, 0, 1000, NULL);

    if(mresult) {
      curl_mfprintf(stderr, "curl_multi_poll() failed: %d\n", mresult);
      result = (CURLcode)1;
      break;
    }

    do {
      int msgq = 0;
      m = curl_multi_info_read(multi, &msgq);
      if(m && (m->msg == CURLMSG_DONE) && (m->easy_handle == curl)) {
        parent_seen = 1;
        parent_result = m->data.result;
        still_running = 0;
      }
    } while(m);

  } while(still_running);

  if(!parent_seen) {
    curl_mfprintf(stderr, "parent transfer never completed\n");
    result = (CURLcode)1;
  }
  else if(parent_result == CURLE_OK) {
    curl_mfprintf(stderr, "FAIL: CURL_PUSH_ERROROUT did not make the "
                  "parent transfer fail\n");
    result = (CURLcode)1;
  }
  else {
    curl_mfprintf(stderr, "OK: parent transfer failed with result %d, "
                  "as CURL_PUSH_ERROROUT demands\n", (int)parent_result);
  }

cleanup:

  if(curl) {
    curl_multi_remove_handle(multi, curl);
    curl_easy_cleanup(curl);
  }
  curl_multi_cleanup(multi);
  curl_global_cleanup();

  return result;
}
