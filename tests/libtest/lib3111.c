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

static CURLcode test_lib3111(const char *URL)
{
  CURLcode result = CURLE_OK;
  CURLSH *share = NULL;
  CURL *victim = NULL;
  CURL *target = NULL;
  curl_socket_t victim_sock = CURL_SOCKET_BAD;
  curl_socket_t target_sock = CURL_SOCKET_BAD;

  global_init(CURL_GLOBAL_ALL);

  share = curl_share_init();
  if(!share) {
    curl_mfprintf(stderr, "curl_share_init() failed\n");
    result = TEST_ERR_MAJOR_BAD;
    goto test_cleanup;
  }
  curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT);

  /* victim: connects while already attached to the share, so its
     connection is the first (id 0) one in the share's pool. */
  easy_init(victim);
  easy_setopt(victim, CURLOPT_URL, URL);
  easy_setopt(victim, CURLOPT_CONNECT_ONLY, 1L);
  easy_setopt(victim, CURLOPT_SHARE, share);
  result = curl_easy_perform(victim);
  if(result) {
    curl_mfprintf(stderr, "victim curl_easy_perform() failed: %s\n",
                  curl_easy_strerror(result));
    goto test_cleanup;
  }
  curl_easy_getinfo(victim, CURLINFO_ACTIVESOCKET, &victim_sock);

  /* target: connects first on its own private pool, so it also ends up
     remembering connection id 0, but in a different pool. */
  easy_init(target);
  easy_setopt(target, CURLOPT_URL, URL);
  easy_setopt(target, CURLOPT_CONNECT_ONLY, 1L);
  result = curl_easy_perform(target);
  if(result) {
    curl_mfprintf(stderr, "target curl_easy_perform() failed: %s\n",
                  curl_easy_strerror(result));
    goto test_cleanup;
  }

  /* attach the CONNECT-sharing share to target after the fact */
  easy_setopt(target, CURLOPT_SHARE, share);

  curl_easy_getinfo(target, CURLINFO_ACTIVESOCKET, &target_sock);

  if(target_sock != CURL_SOCKET_BAD && target_sock == victim_sock) {
    curl_mfprintf(stderr, "target resolved victim's connection (socket %d)\n",
                  (int)target_sock);
    result = TEST_ERR_FAILURE;
  }

test_cleanup:
  curl_easy_cleanup(victim);
  curl_easy_cleanup(target);
  curl_share_cleanup(share);
  curl_global_cleanup();

  return result;
}
