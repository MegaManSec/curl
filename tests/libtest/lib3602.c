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

static CURLcode test_lib3602(const char *URL)
{
  CURL *curl;
  CURLcode rc;
  (void)URL;

  curl = curl_easy_init();
  if(!curl)
    return CURLE_FAILED_INIT;

  rc = curl_easy_setopt(curl, CURLOPT_XOAUTH2_BEARER,
                        "token\r\nX-Injected: yes");
  if(rc != CURLE_BAD_FUNCTION_ARGUMENT) {
    curl_mprintf("setopt with CRLF: expected %d, got %d\n",
                CURLE_BAD_FUNCTION_ARGUMENT, rc);
    curl_easy_cleanup(curl);
    return CURLE_FAILED_INIT;
  }

  rc = curl_easy_setopt(curl, CURLOPT_XOAUTH2_BEARER, "token\ronly");
  if(rc != CURLE_BAD_FUNCTION_ARGUMENT) {
    curl_mprintf("setopt with CR: expected %d, got %d\n",
                CURLE_BAD_FUNCTION_ARGUMENT, rc);
    curl_easy_cleanup(curl);
    return CURLE_FAILED_INIT;
  }

  rc = curl_easy_setopt(curl, CURLOPT_XOAUTH2_BEARER, "token\nonly");
  if(rc != CURLE_BAD_FUNCTION_ARGUMENT) {
    curl_mprintf("setopt with LF: expected %d, got %d\n",
                CURLE_BAD_FUNCTION_ARGUMENT, rc);
    curl_easy_cleanup(curl);
    return CURLE_FAILED_INIT;
  }

  rc = curl_easy_setopt(curl, CURLOPT_XOAUTH2_BEARER, "1ab9cb22bf269a7");
  if(rc != CURLE_OK) {
    curl_mprintf("setopt with a plain token: expected CURLE_OK, got %d\n",
                rc);
    curl_easy_cleanup(curl);
    return CURLE_FAILED_INIT;
  }

  curl_mprintf("ok\n");
  curl_easy_cleanup(curl);
  return CURLE_OK;
}
