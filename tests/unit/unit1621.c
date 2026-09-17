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
#include "unitcheck.h"

#ifndef CURL_DISABLE_PROXY
#include "urldata.h"
#include "creds.h"

static CURLcode test_unit1621(const char *arg)
{
  UNITTEST_BEGIN_SIMPLE

  struct Curl_easy *data = curl_easy_init();
  struct Curl_creds *creds = NULL;

  fail_unless(!!data, "curl_easy_init failed");
  if(data) {
    fail_unless(!Curl_creds_create(NULL, NULL, NULL, NULL, "myproxysvc",
                                   CREDS_OPTION, &creds), "creds create");
    fail_unless(!!creds, "creds create returned NULL");

    fail_unless(socks5_req0_keeps_sasl_service(data, creds, CURLAUTH_GSSAPI),
                "sasl_service must survive when BASIC auth is disabled");
    fail_unless(socks5_req0_keeps_sasl_service(data, creds,
                                               CURLAUTH_GSSAPI |
                                               CURLAUTH_BASIC),
                "sasl_service must survive when BASIC auth is enabled");

    Curl_creds_unlink(&creds);
    curl_easy_cleanup(data);
  }

  UNITTEST_END_SIMPLE
}
#else
static CURLcode test_unit1621(const char *arg)
{
  UNITTEST_BEGIN_SIMPLE
  UNITTEST_END_SIMPLE
}
#endif
