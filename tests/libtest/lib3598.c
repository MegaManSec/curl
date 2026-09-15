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

static CURLcode test_lib3598(const char *URL)
{
  CURL *curl;
  CURLcode rc;
  (void)URL;

  curl = curl_easy_init();
  if(!curl)
    return CURLE_FAILED_INIT;

  /* a CRLF-laden value must not be accepted as a HAProxy client IP: it
     would be interpolated straight into the PROXY protocol line and let
     the caller inject extra header lines */
  rc = curl_easy_setopt(curl, CURLOPT_HAPROXY_CLIENT_IP,
                        "1.2.3.4\r\nGET / HTTP/1.0\r\nHost: evil\r\n");
  if(rc != CURLE_BAD_FUNCTION_ARGUMENT) {
    curl_mprintf("setopt with CRLF: expected %d, got %d\n",
                CURLE_BAD_FUNCTION_ARGUMENT, rc);
    curl_easy_cleanup(curl);
    return CURLE_FAILED_INIT;
  }

  /* garbage that is not an IP address at all must also be rejected */
  rc = curl_easy_setopt(curl, CURLOPT_HAPROXY_CLIENT_IP, "not-an-ip");
  if(rc != CURLE_BAD_FUNCTION_ARGUMENT) {
    curl_mprintf("setopt with garbage: expected %d, got %d\n",
                CURLE_BAD_FUNCTION_ARGUMENT, rc);
    curl_easy_cleanup(curl);
    return CURLE_FAILED_INIT;
  }

  /* a real IPv4 and IPv6 address must still be accepted */
  rc = curl_easy_setopt(curl, CURLOPT_HAPROXY_CLIENT_IP, "1.2.3.4");
  if(rc != CURLE_OK) {
    curl_mprintf("setopt with IPv4: expected CURLE_OK, got %d\n", rc);
    curl_easy_cleanup(curl);
    return CURLE_FAILED_INIT;
  }

  rc = curl_easy_setopt(curl, CURLOPT_HAPROXY_CLIENT_IP, "::1");
  if(rc != CURLE_OK) {
    curl_mprintf("setopt with IPv6: expected CURLE_OK, got %d\n", rc);
    curl_easy_cleanup(curl);
    return CURLE_FAILED_INIT;
  }

  curl_mprintf("ok\n");
  curl_easy_cleanup(curl);
  return CURLE_OK;
}
