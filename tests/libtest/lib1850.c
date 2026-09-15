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
 * Verify that a CURLOPT_XFERINFOFUNCTION abort of the final progress
 * callback for a completing hop stops a pending CURLOPT_FOLLOWLOCATION
 * redirect instead of letting it proceed.
 */

#include "first.h"

struct t1850_ctx {
  int fired;
};

static int t1850_xferinfo(void *clientp,
                          curl_off_t dltotal, curl_off_t dlnow,
                          curl_off_t ultotal, curl_off_t ulnow)
{
  struct t1850_ctx *ctx = (struct t1850_ctx *)clientp;
  (void)dlnow;
  (void)ultotal;
  (void)ulnow;
  if(!ctx->fired && dltotal > 0) {
    ctx->fired = 1;
    curl_mprintf("XFERINFOFUNCTION abort\n");
    return 1;
  }
  return 0;
}

static CURLcode test_lib1850(const char *URL)
{
  CURL *curl;
  CURLcode result = CURLE_OK;
  struct t1850_ctx ctx;
  memset(&ctx, 0, sizeof(ctx));

  global_init(CURL_GLOBAL_ALL);

  easy_init(curl);

  easy_setopt(curl, CURLOPT_URL, URL);
  easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, t1850_xferinfo);
  easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
  easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  easy_setopt(curl, CURLOPT_WRITEFUNCTION, tutil_throwaway_cb);

  result = curl_easy_perform(curl);

test_cleanup:

  curl_easy_cleanup(curl);
  curl_global_cleanup();

  return result;
}
