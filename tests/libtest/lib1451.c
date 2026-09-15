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

#define T1451_CAPBUF_SIZE 8192

struct t1451_capture {
  char buf[T1451_CAPBUF_SIZE];
  size_t len;
};

static size_t t1451_capture_cb(void *arg, const char *data, size_t len)
{
  struct t1451_capture *cap = (struct t1451_capture *)arg;

  if(len && (cap->len + len < sizeof(cap->buf))) {
    memcpy(cap->buf + cap->len, data, len);
    cap->len += len;
  }
  return len;
}

#define t1451_fail_unless(expr, msg)                             \
  do {                                                            \
    if(!(expr)) {                                                 \
      curl_mfprintf(stderr, "%s:%d Assertion '%s' FAILED: %s\n",  \
                    __FILE__, __LINE__, #expr, msg);              \
      errorcount++;                                               \
    }                                                              \
  } while(0)

static int t1451_count(const char *haystack, const char *needle)
{
  int count = 0;
  const char *p = haystack;

  p = strstr(p, needle);
  while(p) {
    count++;
    p = strstr(p + strlen(needle), needle);
  }
  return count;
}

static CURLcode test_lib1451(const char *URL)
{
  int errorcount = 0;
  CURLcode result = CURLE_OK;
  CURLFORMcode rc;
  int formres;
  struct curl_httppost *post;
  struct curl_httppost *last;
  struct t1451_capture cap;

  global_init(CURL_GLOBAL_ALL);

  /* A field with two files: the alias is set on the second file only. It
     must apply to that file's part and not to the first one. */
  post = last = NULL;
  memset(&cap, 0, sizeof(cap));

  rc = curl_formadd(&post, &last,
                    CURLFORM_COPYNAME, "pictures",
                    CURLFORM_FILE, URL,
                    CURLFORM_FILE, URL,
                    CURLFORM_FILENAME, "second-alias.txt",
                    CURLFORM_END);
  t1451_fail_unless(rc == 0, "curl_formadd returned error");

  formres = curl_formget(post, &cap, t1451_capture_cb);
  t1451_fail_unless(formres == 0, "curl_formget returned error");
  cap.buf[cap.len < sizeof(cap.buf) ? cap.len : sizeof(cap.buf) - 1] = '\0';

  t1451_fail_unless(strstr(cap.buf, "filename=\"second-alias.txt\"") != NULL,
                    "alias set on the second file was not honored");
  t1451_fail_unless(t1451_count(cap.buf, "filename=\"second-alias.txt\"") == 1,
                    "alias appeared an unexpected number of times");

  curl_formfree(post);

  /* A field with two files: the alias is set on the first file only. It
     must apply to that file's part alone, not bleed into the second one. */
  post = last = NULL;
  memset(&cap, 0, sizeof(cap));

  rc = curl_formadd(&post, &last,
                    CURLFORM_COPYNAME, "pictures",
                    CURLFORM_FILE, URL,
                    CURLFORM_FILENAME, "first-alias.txt",
                    CURLFORM_FILE, URL,
                    CURLFORM_END);
  t1451_fail_unless(rc == 0, "curl_formadd returned error");

  formres = curl_formget(post, &cap, t1451_capture_cb);
  t1451_fail_unless(formres == 0, "curl_formget returned error");
  cap.buf[cap.len < sizeof(cap.buf) ? cap.len : sizeof(cap.buf) - 1] = '\0';

  t1451_fail_unless(strstr(cap.buf, "filename=\"first-alias.txt\"") != NULL,
                    "alias set on the first file was not honored");
  t1451_fail_unless(t1451_count(cap.buf, "filename=\"first-alias.txt\"") == 1,
                    "alias leaked onto the second file's part");

  curl_formfree(post);

  curl_global_cleanup();

  return errorcount ? TEST_ERR_FAILURE : CURLE_OK;
}
