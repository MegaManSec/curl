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
 * Drive a curl_ws_send() call that only partially flushes its encoded
 * frame (forced via the CURL_WS_CHUNK_EAGAIN debug knob), then let a
 * server-injected PING trigger an automatic PONG while that partial send
 * is still outstanding, before resuming the send with a follow-up call.
 *
 * The client side always reports success: this program's exit code alone
 * cannot show whether the fix is present. The actual verification of
 * whether the frame stream stayed intact happens on the server side in
 * the accompanying Python test.
 */
#include "first.h"

#include "testtrace.h"

#ifndef CURL_DISABLE_WEBSOCKETS

#define T2DESYNC_MSGLEN 4000

static CURLcode t2desync_send_all(CURL *curl, const char *buf, size_t len)
{
  size_t off = 0;
  int tries = 0;

  while(off < len) {
    size_t sent = 0;
    CURLcode result = curl_ws_send(curl, buf + off, len - off, &sent,
                                   0, CURLWS_TEXT);
    if(result == CURLE_AGAIN) {
      if(++tries > 2000)
        return CURLE_OPERATION_TIMEDOUT;
      curlx_wait_ms(10);
      continue;
    }
    else if(result)
      return result;
    curl_mfprintf(stderr, "cli_ws_ping_desync: send off=%zu +%zu\n", off,
                  sent);
    off += sent;
  }
  return CURLE_OK;
}

static CURLcode t2desync_run(CURL *curl)
{
  char *msg;
  CURLcode result;
  size_t sent1 = 0;
  int i, tries;
  bool got_text = FALSE;

  msg = curlx_calloc(1, T2DESYNC_MSGLEN);
  if(!msg)
    return CURLE_OUT_OF_MEMORY;
  for(i = 0; i < T2DESYNC_MSGLEN; ++i)
    msg[i] = (char)('0' + (i % 10));

  /* First call: with CURL_WS_CHUNK_EAGAIN set small enough, this flushes
   * only part of the encoded frame and leaves the rest buffered. */
  result = curl_ws_send(curl, msg, T2DESYNC_MSGLEN, &sent1, 0, CURLWS_TEXT);
  curl_mfprintf(stderr, "cli_ws_ping_desync: send#1 res=%d sent=%zu/%d\n",
                (int)result, sent1, T2DESYNC_MSGLEN);
  if(result) {
    curlx_free(msg);
    return result;
  }
  if(sent1 >= T2DESYNC_MSGLEN) {
    curl_mfprintf(stderr, "cli_ws_ping_desync: send#1 was not partial, "
                  "test did not exercise the window\n");
    curlx_free(msg);
    return CURLE_FAILED_INIT;
  }

  /* Drain the server's PING (auto-PONG) and its TEXT frame. */
  for(tries = 0; tries < 200 && !got_text; ++tries) {
    char rbuf[64];
    size_t rlen;
    const struct curl_ws_frame *meta;
    result = curl_ws_recv(curl, rbuf, sizeof(rbuf), &rlen, &meta);
    if(result == CURLE_AGAIN) {
      curlx_wait_ms(10);
      continue;
    }
    else if(result) {
      curlx_free(msg);
      return result;
    }
    curl_mfprintf(stderr, "cli_ws_ping_desync: recv got=%zu flags=%x\n",
                  rlen, meta ? (unsigned int)meta->flags : 0);
    if(meta && (meta->flags & CURLWS_TEXT))
      got_text = TRUE;
  }
  if(!got_text) {
    curl_mfprintf(stderr, "cli_ws_ping_desync: never saw the server's "
                  "TEXT frame\n");
    curlx_free(msg);
    return CURLE_RECV_ERROR;
  }

  /* Resume the send, as any application must after a partial send. */
  result = t2desync_send_all(curl, msg + sent1, T2DESYNC_MSGLEN - sent1);
  curlx_free(msg);
  return result;
}

#endif

static CURLcode test_cli_ws_ping_desync(const char *URL)
{
#ifndef CURL_DISABLE_WEBSOCKETS
  CURL *curl;
  CURLcode result = CURLE_OK;

  if(curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) {
    curl_mfprintf(stderr, "curl_global_init() failed\n");
    return (CURLcode)3;
  }

  curl = curl_easy_init();
  if(curl) {
    curl_easy_setopt(curl, CURLOPT_URL, URL);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "ws-ping-desync");
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 2L); /* websocket style */
    result = curl_easy_perform(curl);
    curl_mfprintf(stderr, "curl_easy_perform() returned %d\n", (int)result);
    if(result == CURLE_OK)
      result = t2desync_run(curl);

    if(!result)
      ws_close(curl);
    curl_easy_cleanup(curl);
  }
  curl_global_cleanup();
  return result;

#else /* !CURL_DISABLE_WEBSOCKETS */
  (void)URL;
  curl_mfprintf(stderr, "WebSockets not enabled in libcurl\n");
  return (CURLcode)1;
#endif /* CURL_DISABLE_WEBSOCKETS */
}
