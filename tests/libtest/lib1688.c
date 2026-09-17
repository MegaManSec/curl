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

/* Queue more than one internal notification chunk's worth of
 * CURLMNOTIFY_EASY_DONE events before they get dispatched, and verify
 * that every single one of them reaches the notify callback. */

#include "first.h"

#define NTFY_NUM_HANDLES 300

struct notify_ctx {
  int count;
};

static void notify_cb(CURLM *multi, unsigned int notification,
                      CURL *easy, void *notifyp)
{
  struct notify_ctx *ctx = notifyp;
  (void)multi;
  (void)easy;
  if(notification == CURLMNOTIFY_EASY_DONE)
    ctx->count++;
}

static size_t discard_cb(char *ptr, size_t size, size_t nmemb, void *userp)
{
  (void)ptr;
  (void)userp;
  return size * nmemb;
}

static CURLcode test_lib1688(const char *URL)
{
  CURLM *multi = NULL;
  CURL *handles[NTFY_NUM_HANDLES];
  struct notify_ctx ctx;
  int i;
  int running = 0;
  int numfds;
  int loops;
  CURLcode result = CURLE_OK;

  memset(handles, 0, sizeof(handles));
  memset(&ctx, 0, sizeof(ctx));

  global_init(CURL_GLOBAL_ALL);
  multi_init(multi);

  multi_setopt(multi, CURLMOPT_NOTIFYFUNCTION, notify_cb);
  multi_setopt(multi, CURLMOPT_NOTIFYDATA, &ctx);

  if(curl_multi_notify_enable(multi, CURLMNOTIFY_EASY_DONE)) {
    curl_mfprintf(stderr, "%s:%d curl_multi_notify_enable() failed\n",
                  __FILE__, __LINE__);
    result = CURLE_FAILED_INIT;
    goto test_cleanup;
  }

  for(i = 0; i < NTFY_NUM_HANDLES; i++) {
    easy_init(handles[i]);
    easy_setopt(handles[i], CURLOPT_URL, URL);
    easy_setopt(handles[i], CURLOPT_WRITEFUNCTION, discard_cb);
    multi_add_handle(multi, handles[i]);
  }

  running = NTFY_NUM_HANDLES;
  for(loops = 0; running && (loops < 1000); loops++) {
    multi_perform(multi, &running);
    if(running)
      multi_poll(multi, NULL, 0, 100, &numfds);
  }

  if(running) {
    curl_mfprintf(stderr, "%s:%d %d transfers still running, expected 0\n",
                  __FILE__, __LINE__, running);
    result = CURLE_FAILED_INIT;
    goto test_cleanup;
  }

  curl_mprintf("notify count: %d\n", ctx.count);
  if(ctx.count != NTFY_NUM_HANDLES) {
    curl_mfprintf(stderr, "%s:%d expected %d EASY_DONE notifications, "
                  "got %d\n", __FILE__, __LINE__, NTFY_NUM_HANDLES,
                  ctx.count);
    result = CURLE_FAILED_INIT;
    goto test_cleanup;
  }

test_cleanup:
  for(i = 0; i < NTFY_NUM_HANDLES; i++) {
    if(handles[i]) {
      curl_multi_remove_handle(multi, handles[i]);
      curl_easy_cleanup(handles[i]);
    }
  }
  curl_multi_cleanup(multi);
  curl_global_cleanup();
  return result;
}
