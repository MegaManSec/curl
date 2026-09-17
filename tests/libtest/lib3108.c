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

/* Reentrant CURLOPT_CLOSESOCKETFUNCTION reproduction for the connection
 * pool iterator use-after-free: while curl_multi_perform() on 'multi1'
 * scans a bundle with two idle, over-age connections and closes the
 * first one, its closesocket callback reenters libcurl via a second
 * multi handle sharing the same CONNECT pool. That reentrant scan used
 * to be able to close the second connection, freeing the node the outer
 * scan had already advanced to.
 */

static CURLM *t3108_multi2;
static CURL *t3108_easy2;
static int t3108_armed;
static int t3108_fired;

static int t3108_closesocket(void *clientp, curl_socket_t item)
{
  (void)clientp;
  if(t3108_armed && !t3108_fired) {
    int running = 0;
    t3108_fired = 1;
    curl_multi_perform(t3108_multi2, &running);
  }
  return sclose(item);
}

static CURLcode test_lib3108(const char *URL)
{
  CURLcode result = CURLE_OK;
  CURLSH *share = NULL;
  CURLM *multi1 = NULL;
  CURL *warmup = NULL;
  CURL *easy3 = NULL;
  int running;

  start_test_timing();
  global_init(CURL_GLOBAL_ALL);

  share = curl_share_init();
  if(!share) {
    curl_mfprintf(stderr, "curl_share_init() failed\n");
    goto test_cleanup;
  }
  curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT);

  /* Populate the shared pool with two idle connections to the same
   * destination bundle. */
  easy_init(warmup);
  easy_setopt(warmup, CURLOPT_URL, URL);
  easy_setopt(warmup, CURLOPT_SHARE, share);
  easy_setopt(warmup, CURLOPT_CLOSESOCKETFUNCTION, t3108_closesocket);
  result = curl_easy_perform(warmup);
  curl_easy_cleanup(warmup);
  warmup = NULL;
  if(result)
    goto test_cleanup;

  easy_init(warmup);
  easy_setopt(warmup, CURLOPT_URL, URL);
  easy_setopt(warmup, CURLOPT_SHARE, share);
  easy_setopt(warmup, CURLOPT_CLOSESOCKETFUNCTION, t3108_closesocket);
  easy_setopt(warmup, CURLOPT_FRESH_CONNECT, 1L);
  result = curl_easy_perform(warmup);
  curl_easy_cleanup(warmup);
  warmup = NULL;
  if(result)
    goto test_cleanup;

  /* Let both connections age past the 1 second CURLOPT_MAXAGE_CONN
   * threshold used below. */
  curlx_wait_ms(1200);

  /* The reentrant handle: added to multi2 but only driven from inside
   * the closesocket callback fired by the outer scan below. */
  multi_init(t3108_multi2);
  easy_init(t3108_easy2);
  easy_setopt(t3108_easy2, CURLOPT_URL, URL);
  easy_setopt(t3108_easy2, CURLOPT_SHARE, share);
  easy_setopt(t3108_easy2, CURLOPT_MAXAGE_CONN, 1L);
  multi_add_handle(t3108_multi2, t3108_easy2);

  /* The outer scan: finds the two over-age connections in the bundle
   * and closes the first one, triggering the reentrant callback. */
  multi_init(multi1);
  easy_init(easy3);
  easy_setopt(easy3, CURLOPT_URL, URL);
  easy_setopt(easy3, CURLOPT_SHARE, share);
  easy_setopt(easy3, CURLOPT_MAXAGE_CONN, 1L);
  multi_add_handle(multi1, easy3);

  t3108_armed = 1;
  multi_perform(multi1, &running);
  while(running) {
    int numfds;
    multi_poll(multi1, NULL, 0, 1000, &numfds);
    multi_perform(multi1, &running);
    abort_on_test_timeout();
  }

test_cleanup:
  if(multi1 && easy3)
    curl_multi_remove_handle(multi1, easy3);
  curl_easy_cleanup(easy3);
  curl_multi_cleanup(multi1);
  if(t3108_multi2 && t3108_easy2)
    curl_multi_remove_handle(t3108_multi2, t3108_easy2);
  curl_easy_cleanup(t3108_easy2);
  curl_multi_cleanup(t3108_multi2);
  curl_easy_cleanup(warmup);
  curl_share_cleanup(share);
  curl_global_cleanup();

  return result;
}
