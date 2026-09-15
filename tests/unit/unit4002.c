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

static CURLcode test_unit4002(const char *arg)
{
  UNITTEST_BEGIN_SIMPLE

  struct curl_httppost *post = NULL;
  struct curl_httppost *last = NULL;
  char buffer[] = "test buffer";
  CURLFORMcode rc;

  rc = curl_formadd(&post, &last,
                    CURLFORM_COPYNAME, "name",
                    CURLFORM_BUFFER, NULL,
                    CURLFORM_BUFFERPTR, buffer,
                    CURLFORM_BUFFERLENGTH, (long)sizeof(buffer),
                    CURLFORM_END);
  fail_unless(rc == CURL_FORMADD_NULL,
              "CURLFORM_BUFFER with a NULL argument should return "
              "CURL_FORMADD_NULL");
  fail_unless(!post && !last, "no form should have been created");

  rc = curl_formadd(&post, &last,
                    CURLFORM_COPYNAME, "name",
                    CURLFORM_FILE, "curl.h",
                    CURLFORM_FILENAME, NULL,
                    CURLFORM_END);
  fail_unless(rc == CURL_FORMADD_NULL,
              "CURLFORM_FILENAME with a NULL argument should return "
              "CURL_FORMADD_NULL");
  fail_unless(!post && !last, "no form should have been created");

  curl_formfree(post);

  UNITTEST_END_SIMPLE
}
