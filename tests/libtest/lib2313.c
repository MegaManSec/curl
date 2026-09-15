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

#ifndef CURL_DISABLE_WEBSOCKETS

static size_t t2313_write_cb(char *buffer, size_t size, size_t nitems,
                             void *p)
{
  size_t len = nitems;
  size_t i;
  (void)p;
  (void)size;

  for(i = 0; i < len; ++i)
    curl_mprintf("%02x ", (unsigned char)buffer[i]);
  curl_mprintf("\n");
  return nitems;
}
#endif

static CURLcode test_lib2313(const char *URL)
{
#ifndef CURL_DISABLE_WEBSOCKETS
  CURL *curl;
  CURLcode result = CURLE_OK;

  global_init(CURL_GLOBAL_ALL);

  curl = curl_easy_init();
  if(curl) {
    curl_easy_setopt(curl, CURLOPT_URL, URL);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "websocket/2313");
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, t2313_write_cb);
    result = curl_easy_perform(curl);
    curl_mfprintf(stderr, "t2313: curl_easy_perform() returned %d\n",
                 (int)result);
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
