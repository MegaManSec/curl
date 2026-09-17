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

/* Regression test for a stale-mid notification being misdelivered to a
 * different, unrelated easy handle.
 *
 * Two easy handles 'a' and 'b' are added and run to completion together,
 * so both their CURLMNOTIFY_EASY_DONE notifications end up queued in the
 * same, not yet dispatched, chunk. Before that happens, a priming loop
 * repeatedly adds and immediately removes a throwaway easy handle. Each
 * such add() call claims the next never-used row in the multi handle's
 * internal transfer-id table, so this walks the table's round-robin
 * allocation pointer up to the top of its capacity without ever growing
 * it (the table always keeps a large chunk of never-yet-used rows free,
 * see multi_xfers_add() in lib/multi.c, so growing is not triggered by
 * this add-then-remove churn). The exact number of iterations below
 * ties to the table's current default initial capacity of 128 rows,
 * of which the internal admin handle plus 'a' and 'b' already claim 3.
 *
 * Once the notify callback for whichever of 'a'/'b' is dispatched first
 * fires, it removes the *other* one (whose notification is still queued
 * right behind, undispatched) and adds a new handle 'c'. Because the
 * allocation pointer is now sitting at the top of the table, this add()
 * wraps around and reuses the row just freed by the removal, i.e. 'c'
 * gets assigned the same internal id the removed handle had.
 *
 * When the dispatch loop then reaches the queued notification meant for
 * the removed handle, it must not be delivered against 'c', which is
 * merely occupying the same, recycled internal id and has not completed
 * at all. Correct behavior is for the stale notification to be dropped,
 * so the total tally is exactly 2 real EASY_DONE notifications ('a' and
 * later 'c' once it genuinely finishes), never 3.
 */

#include "first.h"

#define NTFY1719_PRIME_ITERATIONS 125

struct ntfy1719_ctx {
  CURLM *multi;
  CURL *a;
  CURL *b;
  CURL *c;
  const char *url;
  int count;
  int add_c_failed;
  CURL *seen[4];
};

static size_t discard1719_cb(char *ptr, size_t size, size_t nmemb,
                             void *userp)
{
  (void)ptr;
  (void)userp;
  return size * nmemb;
}

static void notify1719_cb(CURLM *multi, unsigned int notification,
                          CURL *easy, void *notifyp)
{
  struct ntfy1719_ctx *ctx = notifyp;
  (void)multi;

  if(notification != CURLMNOTIFY_EASY_DONE)
    return;

  if(ctx->count < (int)(sizeof(ctx->seen) / sizeof(ctx->seen[0])))
    ctx->seen[ctx->count] = easy;
  ctx->count++;

  if(ctx->count == 1) {
    /* remove whichever of 'a'/'b' did not just complete: its own
     * notification is still queued behind this one */
    CURL *other = (easy == ctx->a) ? ctx->b : ctx->a;
    curl_multi_remove_handle(ctx->multi, other);
    curl_easy_cleanup(other);
    if(other == ctx->b)
      ctx->b = NULL;
    else
      ctx->a = NULL;

    ctx->c = curl_easy_init();
    if(ctx->c) {
      curl_easy_setopt(ctx->c, CURLOPT_URL, ctx->url);
      curl_easy_setopt(ctx->c, CURLOPT_WRITEFUNCTION, discard1719_cb);
      if(curl_multi_add_handle(ctx->multi, ctx->c))
        ctx->add_c_failed = 1;
    }
    else
      ctx->add_c_failed = 1;
  }
}

static CURLcode test_lib1719(const char *URL)
{
  CURLM *multi = NULL;
  struct ntfy1719_ctx ctx;
  int i;
  int running = 1;
  int numfds;
  int loops;
  CURLcode result = CURLE_OK;

  memset(&ctx, 0, sizeof(ctx));
  ctx.url = URL;

  global_init(CURL_GLOBAL_ALL);
  multi_init(multi);
  ctx.multi = multi;

  multi_setopt(multi, CURLMOPT_NOTIFYFUNCTION, notify1719_cb);
  multi_setopt(multi, CURLMOPT_NOTIFYDATA, &ctx);

  if(curl_multi_notify_enable(multi, CURLMNOTIFY_EASY_DONE)) {
    curl_mfprintf(stderr, "%s:%d curl_multi_notify_enable() failed\n",
                  __FILE__, __LINE__);
    result = CURLE_FAILED_INIT;
    goto test_cleanup;
  }

  easy_init(ctx.a);
  easy_setopt(ctx.a, CURLOPT_URL, URL);
  easy_setopt(ctx.a, CURLOPT_WRITEFUNCTION, discard1719_cb);
  multi_add_handle(multi, ctx.a);

  easy_init(ctx.b);
  easy_setopt(ctx.b, CURLOPT_URL, URL);
  easy_setopt(ctx.b, CURLOPT_WRITEFUNCTION, discard1719_cb);
  multi_add_handle(multi, ctx.b);

  for(i = 0; i < NTFY1719_PRIME_ITERATIONS; i++) {
    CURL *t = curl_easy_init();
    if(!t) {
      curl_mfprintf(stderr, "%s:%d curl_easy_init() failed\n",
                    __FILE__, __LINE__);
      result = CURLE_FAILED_INIT;
      goto test_cleanup;
    }
    curl_easy_setopt(t, CURLOPT_URL, URL);
    if(curl_multi_add_handle(multi, t)) {
      curl_mfprintf(stderr, "%s:%d curl_multi_add_handle() failed\n",
                    __FILE__, __LINE__);
      curl_easy_cleanup(t);
      result = CURLE_FAILED_INIT;
      goto test_cleanup;
    }
    curl_multi_remove_handle(multi, t);
    curl_easy_cleanup(t);
  }

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

  if(ctx.add_c_failed) {
    curl_mfprintf(stderr, "%s:%d failed to add replacement handle\n",
                  __FILE__, __LINE__);
    result = CURLE_FAILED_INIT;
    goto test_cleanup;
  }

  curl_mprintf("notify count: %d\n", ctx.count);
  if(ctx.count != 2) {
    curl_mfprintf(stderr, "%s:%d expected 2 EASY_DONE notifications, "
                  "got %d\n", __FILE__, __LINE__, ctx.count);
    result = CURLE_FAILED_INIT;
    goto test_cleanup;
  }

  if(ctx.seen[1] != ctx.c) {
    curl_mfprintf(stderr, "%s:%d second notification was misattributed, "
                  "did not refer to the replacement handle\n",
                  __FILE__, __LINE__);
    result = CURLE_FAILED_INIT;
    goto test_cleanup;
  }

test_cleanup:
  if(ctx.a) {
    curl_multi_remove_handle(multi, ctx.a);
    curl_easy_cleanup(ctx.a);
  }
  if(ctx.b) {
    curl_multi_remove_handle(multi, ctx.b);
    curl_easy_cleanup(ctx.b);
  }
  if(ctx.c) {
    curl_multi_remove_handle(multi, ctx.c);
    curl_easy_cleanup(ctx.c);
  }
  curl_multi_cleanup(multi);
  curl_global_cleanup();
  return result;
}
