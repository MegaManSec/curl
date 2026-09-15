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
 * Reentering curl_ws_recv() on the same handle from within the SSL trace
 * callback fired synchronously from inside an outer curl_ws_recv() call
 * must not be allowed to race the outer call for the receive buffer's
 * write position.
 */
#include "first.h"

#ifndef CURL_DISABLE_WEBSOCKETS

struct t2312_ctx {
  CURL *curl;
  bool armed;
  int reentries;
  CURLcode reentrant_result;
};

static int t2312_debug_cb(CURL *handle, curl_infotype type,
                          char *data, size_t size, void *userp)
{
  struct t2312_ctx *ctx = userp;
  (void)handle;
  (void)data;
  (void)size;
  if(type == CURLINFO_SSL_DATA_IN && ctx->armed && !ctx->reentries) {
    size_t rlen;
    char buffer[16];
    const struct curl_ws_frame *meta;

    ctx->reentries++;
    ctx->reentrant_result = curl_ws_recv(ctx->curl, buffer, sizeof(buffer),
                                         &rlen, &meta);
    curl_mfprintf(stderr, "t2312: reentrant curl_ws_recv() -> %d\n",
                  (int)ctx->reentrant_result);
  }
  return 0;
}

static CURLcode t2312_websocket(CURL *curl, struct t2312_ctx *ctx)
{
  size_t rlen;
  const struct curl_ws_frame *meta;
  char buffer[256];
  size_t bufidx = 0;
  curl_off_t bytesleft = 1; /* enter the loop at least once */
  CURLcode result = CURLE_OK;
  int tries = 0;

  ctx->armed = TRUE;
  while(bytesleft && (tries++ < 50)) {
    result = curl_ws_recv(curl, buffer + bufidx, sizeof(buffer) - bufidx,
                          &rlen, &meta);
    if(result == CURLE_AGAIN) {
      curlx_wait_ms(100);
      continue;
    }
    else if(result)
      break;
    bufidx += rlen;
    bytesleft = meta->bytesleft;
    curl_mfprintf(stderr, "t2312: curl_ws_recv() got %zu bytes, "
                  "%" CURL_FORMAT_CURL_OFF_T " left\n", rlen, bytesleft);
  }
  ctx->armed = FALSE;
  curl_mfprintf(stderr, "t2312: outer curl_ws_recv() done -> %d, "
                "%zu bytes total\n", (int)result, bufidx);
  if(result)
    return result;

  if(bufidx != 4 || memcmp(buffer, "boop", 4)) {
    curl_mfprintf(stderr, "t2312: unexpected payload\n");
    return CURLE_FAILED_INIT;
  }

  if(!ctx->reentries) {
    curl_mfprintf(stderr, "t2312: debug callback never reentered, "
                  "test did not exercise the guard\n");
    return CURLE_FAILED_INIT;
  }
  if(ctx->reentrant_result != CURLE_RECURSIVE_API_CALL) {
    curl_mfprintf(stderr, "t2312: reentrant curl_ws_recv() returned %d, "
                  "expected CURLE_RECURSIVE_API_CALL\n",
                  (int)ctx->reentrant_result);
    return CURLE_FAILED_INIT;
  }
  return CURLE_OK;
}
#endif

static CURLcode test_lib2312(const char *URL)
{
#ifndef CURL_DISABLE_WEBSOCKETS
  CURL *curl;
  CURLcode result = CURLE_OK;
  struct t2312_ctx ctx;

  memset(&ctx, 0, sizeof(ctx));

  global_init(CURL_GLOBAL_ALL);

  curl = curl_easy_init();
  if(curl) {
    ctx.curl = curl;
    curl_easy_setopt(curl, CURLOPT_URL, URL);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "websocket/2312");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, t2312_debug_cb);
    curl_easy_setopt(curl, CURLOPT_DEBUGDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 2L); /* websocket style */
    result = curl_easy_perform(curl);
    curl_mfprintf(stderr, "curl_easy_perform() returned %d\n", (int)result);
    if(result == CURLE_OK)
      result = t2312_websocket(curl, &ctx);

    /* always cleanup */
    curl_easy_cleanup(curl);
  }
  curl_global_cleanup();
  return result;
#else
  (void)URL;
  curl_mfprintf(stderr, "Missing support\n");
  return CURLE_UNSUPPORTED_PROTOCOL;
#endif
}
