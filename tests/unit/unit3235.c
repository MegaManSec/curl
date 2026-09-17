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

/* Boundary line size, matching boundarysize in multipart_size(). */
#define BOUNDARY_OVERHEAD (4 + MIME_BOUNDARY_LEN + 2)
/* CRLF added after a part's (here empty) header block. */
#define HEADER_CRLF 2
/* Total overhead multipart_size() adds around a single headerless part:
   the leading and trailing boundary lines plus the header CRLF. */
#define SINGLE_PART_OVERHEAD (2 * BOUNDARY_OVERHEAD + HEADER_CRLF)

static size_t t3235_read(char *buffer, size_t size, size_t nitems,
                         void *instream)
{
  (void)buffer;
  (void)size;
  (void)nitems;
  (void)instream;
  return 0;
}

static CURLcode t3235_setup(void)
{
  CURLcode result = CURLE_OK;

  global_init(CURL_GLOBAL_ALL);
  return result;
}

static void t3235_stop(CURL *easy)
{
  curl_easy_cleanup(easy);
  curl_global_cleanup();
}

static CURLcode test_unit3235(const char *arg)
{
  CURL *easy;
  curl_mime *mime;
  curl_mimepart *part;
  curl_off_t sz;

  UNITTEST_BEGIN(t3235_setup())

  /* Direct checks of the checked-add helper. */
  fail_unless(mime_size_add(0, 0) == 0, "0 + 0");
  fail_unless(mime_size_add(41, 1) == 42, "41 + 1");
  fail_unless(mime_size_add(CURL_OFF_T_MAX, 0) == CURL_OFF_T_MAX,
              "MAX + 0 must not overflow");
  fail_unless(mime_size_add(CURL_OFF_T_MAX - 1, 1) == CURL_OFF_T_MAX,
              "(MAX - 1) + 1 must not overflow");
  fail_unless(mime_size_add(CURL_OFF_T_MAX, 1) == -1,
              "MAX + 1 must be reported as unknown size");
  fail_unless(mime_size_add(CURL_OFF_T_MAX / 2 + 1, CURL_OFF_T_MAX / 2 + 1) ==
              -1, "overflowing halves must be reported as unknown size");
  fail_unless(mime_size_add(-1, 5) == -1, "negative operand stays unknown");
  fail_unless(mime_size_add(5, -1) == -1, "negative operand stays unknown");

  easy = curl_easy_init();
  abort_unless(easy, "curl_easy_init()");

  /* A small, ordinary multipart body: size must still be computed exactly
     (no regression for the common, non-overflowing case). */
  mime = curl_mime_init(easy);
  abort_unless(mime, "curl_mime_init()");
  part = curl_mime_addpart(mime);
  abort_unless(part, "curl_mime_addpart()");
  fail_unless(curl_mime_data_cb(part, 10, t3235_read, NULL, NULL, NULL) ==
              CURLE_OK, "curl_mime_data_cb()");
  sz = multipart_size(mime);
  fail_unless(sz == SINGLE_PART_OVERHEAD + 10,
              "small known-size multipart must add up exactly");
  curl_mime_free(mime);

  /* A single part sized so the total lands exactly on CURL_OFF_T_MAX: must
     still report the exact (large but valid) size, not unknown. */
  mime = curl_mime_init(easy);
  abort_unless(mime, "curl_mime_init()");
  part = curl_mime_addpart(mime);
  abort_unless(part, "curl_mime_addpart()");
  fail_unless(curl_mime_data_cb(part,
                                 CURL_OFF_T_MAX - SINGLE_PART_OVERHEAD,
                                 t3235_read, NULL, NULL, NULL) == CURLE_OK,
              "curl_mime_data_cb()");
  sz = multipart_size(mime);
  fail_unless(sz == CURL_OFF_T_MAX,
              "total landing exactly on CURL_OFF_T_MAX must not be flagged"
              " as overflow");
  curl_mime_free(mime);

  /* One byte more: the total now exceeds CURL_OFF_T_MAX by one and must be
     reported as unknown size (-1), never as a wrapped/negative value. */
  mime = curl_mime_init(easy);
  abort_unless(mime, "curl_mime_init()");
  part = curl_mime_addpart(mime);
  abort_unless(part, "curl_mime_addpart()");
  fail_unless(curl_mime_data_cb(part,
                                 CURL_OFF_T_MAX - SINGLE_PART_OVERHEAD + 1,
                                 t3235_read, NULL, NULL, NULL) == CURLE_OK,
              "curl_mime_data_cb()");
  sz = multipart_size(mime);
  fail_unless(sz == -1,
              "total one byte over CURL_OFF_T_MAX must be reported unknown");
  curl_mime_free(mime);

  /* Three huge parts, as described in the report: each individually valid,
     but their sum overflows curl_off_t several times over. */
  mime = curl_mime_init(easy);
  abort_unless(mime, "curl_mime_init()");
  part = curl_mime_addpart(mime);
  abort_unless(part, "curl_mime_addpart()");
  fail_unless(curl_mime_data_cb(part, CURL_OFF_T_MAX / 2, t3235_read,
                                 NULL, NULL, NULL) == CURLE_OK,
              "curl_mime_data_cb()");
  part = curl_mime_addpart(mime);
  abort_unless(part, "curl_mime_addpart()");
  fail_unless(curl_mime_data_cb(part, CURL_OFF_T_MAX / 2, t3235_read,
                                 NULL, NULL, NULL) == CURLE_OK,
              "curl_mime_data_cb()");
  part = curl_mime_addpart(mime);
  abort_unless(part, "curl_mime_addpart()");
  fail_unless(curl_mime_data_cb(part, CURL_OFF_T_MAX / 2, t3235_read,
                                 NULL, NULL, NULL) == CURLE_OK,
              "curl_mime_data_cb()");
  sz = multipart_size(mime);
  fail_unless(sz == -1,
              "three huge parts must be reported unknown, not wrapped"
              " negative");
  curl_mime_free(mime);

  UNITTEST_END(t3235_stop(easy))
}

#else /* mime disabled */

static CURLcode test_unit3235(const char *arg)
{
  UNITTEST_BEGIN_SIMPLE
  puts("nothing to do when mime is disabled");
  UNITTEST_END_SIMPLE
}

#endif
