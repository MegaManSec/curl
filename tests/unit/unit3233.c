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
#include "urldata.h"
#include "mime.h"

#if !defined(CURL_DISABLE_MIME) && (!defined(CURL_DISABLE_HTTP) || \
    !defined(CURL_DISABLE_SMTP) || !defined(CURL_DISABLE_IMAP))

static CURLcode t3233_setup(void)
{
  CURLcode result = CURLE_OK;
  global_init(CURL_GLOBAL_ALL);
  return result;
}

static void t3233_stop(CURL *easy, curl_mime *mime)
{
  curl_mime_free(mime);
  if(easy)
    curl_easy_cleanup(easy);
  curl_global_cleanup();
}

static CURLcode test_unit3233(const char *arg)
{
  CURL *easy = NULL;
  curl_mime *mime = NULL;
  curl_mimepart *part;

  UNITTEST_BEGIN(t3233_setup())

  easy = curl_easy_init();
  abort_unless(easy, "curl_easy_init()");
  mime = curl_mime_init(easy);
  abort_unless(mime, "curl_mime_init()");

  part = curl_mime_addpart(mime);
  abort_unless(part, "curl_mime_addpart()");
  fail_unless(curl_mime_type(part, "text/plain\r\nX-Injected: 1") ==
              CURLE_BAD_FUNCTION_ARGUMENT,
              "CRLF mimetype accepted");

  fail_unless(curl_mime_type(part, "text/plain\nX-Injected: 1") ==
              CURLE_BAD_FUNCTION_ARGUMENT,
              "LF mimetype accepted");

  fail_unless(curl_mime_type(part, "text/plain\rX-Injected: 1") ==
              CURLE_BAD_FUNCTION_ARGUMENT,
              "CR mimetype accepted");

  fail_unless(curl_mime_type(part, "text/plain") == CURLE_OK,
              "plain mimetype rejected");

  UNITTEST_END(t3233_stop(easy, mime))
}

#else /* mime disabled */

static CURLcode test_unit3233(const char *arg)
{
  UNITTEST_BEGIN_SIMPLE
  puts("nothing to do when mime is disabled");
  UNITTEST_END_SIMPLE
}

#endif
