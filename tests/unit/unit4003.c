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

static size_t discard_callback(void *arg, const char *buf, size_t len)
{
  (void)buf;
  *((size_t *)arg) += len;
  return len;
}

static CURLcode test_unit4003(const char *arg)
{
  UNITTEST_BEGIN_SIMPLE

  struct curl_httppost *post = NULL;
  struct curl_httppost *last = NULL;
  struct curl_httppost *saved_last;
  CURLFORMcode rc;
  int formres;
  size_t total_size = 0;

  rc = curl_formadd(&post, &last,
                    CURLFORM_COPYNAME, "name",
                    CURLFORM_FILE, "Makefile.inc",
                    CURLFORM_CONTENTTYPE, "text/plain",
                    CURLFORM_END);
  fail_unless(rc == CURL_FORMADD_OK, "curl_formadd returned error");
  fail_unless(post && last, "a form should have been created");
  saved_last = last;

  rc = curl_formadd(&post, &last,
                    CURLFORM_COPYNAME, "name2",
                    CURLFORM_FILE, "Makefile.inc",
                    CURLFORM_CONTENTTYPE, "text/plain",
                    CURLFORM_CONTENTTYPE, "text/html",
                    CURLFORM_END);
  fail_unless(rc == CURL_FORMADD_OPTION_TWICE,
              "a second CURLFORM_CONTENTTYPE for the same file field "
              "should return CURL_FORMADD_OPTION_TWICE");
  fail_unless(last == saved_last,
              "the rejected field must not be linked into the list");
  fail_unless(!saved_last->next,
              "no extra field should have been added to the list");
  fail_unless(!saved_last->more,
              "the original field must not gain a corrupt extra node");

  formres = curl_formget(post, &total_size, discard_callback);
  fail_unless(formres == 0,
              "curl_formget must still work on the unaffected field");

  curl_formfree(post);

  UNITTEST_END_SIMPLE
}
