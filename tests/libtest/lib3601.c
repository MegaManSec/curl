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

#ifndef CURL_DISABLE_RTSP

static CURLcode test_lib3601(const char *URL)
{
  CURL *curl;
  CURLcode rc;
  (void)URL;

  curl = curl_easy_init();
  if(!curl)
    return CURLE_FAILED_INIT;

  rc = curl_easy_setopt(curl, CURLOPT_RTSP_STREAM_URI,
                        "rtsp://example.com/\r\nTEARDOWN rtsp://x/ RTSP/1.0");
  if(rc != CURLE_BAD_FUNCTION_ARGUMENT) {
    curl_mprintf("RTSP_STREAM_URI with CRLF: expected %d, got %d\n",
                CURLE_BAD_FUNCTION_ARGUMENT, rc);
    curl_easy_cleanup(curl);
    return CURLE_FAILED_INIT;
  }

  rc = curl_easy_setopt(curl, CURLOPT_RTSP_SESSION_ID,
                        "abc\r\nX-Injected: 1");
  if(rc != CURLE_BAD_FUNCTION_ARGUMENT) {
    curl_mprintf("RTSP_SESSION_ID with CRLF: expected %d, got %d\n",
                CURLE_BAD_FUNCTION_ARGUMENT, rc);
    curl_easy_cleanup(curl);
    return CURLE_FAILED_INIT;
  }

  rc = curl_easy_setopt(curl, CURLOPT_RTSP_TRANSPORT,
                        "RTP/AVP;unicast\r\nX-Injected: 1");
  if(rc != CURLE_BAD_FUNCTION_ARGUMENT) {
    curl_mprintf("RTSP_TRANSPORT with CRLF: expected %d, got %d\n",
                CURLE_BAD_FUNCTION_ARGUMENT, rc);
    curl_easy_cleanup(curl);
    return CURLE_FAILED_INIT;
  }

  rc = curl_easy_setopt(curl, CURLOPT_RTSP_STREAM_URI,
                        "rtsp://example.com/twister/video");
  if(rc != CURLE_OK) {
    curl_mprintf("RTSP_STREAM_URI benign: expected CURLE_OK, got %d\n", rc);
    curl_easy_cleanup(curl);
    return CURLE_FAILED_INIT;
  }

  rc = curl_easy_setopt(curl, CURLOPT_RTSP_SESSION_ID, "abc123");
  if(rc != CURLE_OK) {
    curl_mprintf("RTSP_SESSION_ID benign: expected CURLE_OK, got %d\n", rc);
    curl_easy_cleanup(curl);
    return CURLE_FAILED_INIT;
  }

  rc = curl_easy_setopt(curl, CURLOPT_RTSP_TRANSPORT,
                        "RTP/AVP;unicast;client_port=4588-4589");
  if(rc != CURLE_OK) {
    curl_mprintf("RTSP_TRANSPORT benign: expected CURLE_OK, got %d\n", rc);
    curl_easy_cleanup(curl);
    return CURLE_FAILED_INIT;
  }

  curl_mprintf("ok\n");
  curl_easy_cleanup(curl);
  return CURLE_OK;
}

#else

static CURLcode test_lib3601(const char *URL)
{
  (void)URL;
  curl_mprintf("ok\n");
  return CURLE_OK;
}

#endif
