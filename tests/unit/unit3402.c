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
#include "curl_share.h"

static CURLcode test_unit3402(const char *arg)
{
  CURLSH *sh;
  struct Curl_share *share;
  CURLSHcode rc;

  UNITTEST_BEGIN_SIMPLE

  sh = curl_share_init();
  abort_unless(sh, "curl_share_init failed");
  share = (struct Curl_share *)sh;

  rc = curl_share_setopt(sh, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT);
  fail_unless(rc == CURLSHE_OK, "share of CURL_LOCK_DATA_CONNECT failed");
  fail_unless(share->cpool.initialized,
              "cpool.initialized not set after share");
  fail_unless(share->cpool.dest2bundle.slots,
              "cpool hash not allocated after share");

  rc = curl_share_setopt(sh, CURLSHOPT_UNSHARE, CURL_LOCK_DATA_CONNECT);
  fail_unless(rc == CURLSHE_OK, "unshare of CURL_LOCK_DATA_CONNECT failed");
  fail_unless(!share->cpool.initialized,
              "cpool.initialized not cleared by unshare");
  fail_unless(!share->cpool.dest2bundle.table,
              "cpool hash table not destroyed by unshare");
  fail_unless(!share->cpool.dest2bundle.slots,
              "cpool hash slots not cleared by unshare");

  rc = curl_share_setopt(sh, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT);
  fail_unless(rc == CURLSHE_OK, "re-share of CURL_LOCK_DATA_CONNECT failed");
  fail_unless(share->cpool.initialized,
              "cpool not reinitialized after re-share");
  fail_unless(share->cpool.dest2bundle.slots,
              "cpool hash not freshly allocated after re-share");
  fail_unless(!share->cpool.num_conn,
              "freshly reinitialized pool should hold no connections");

  curl_share_cleanup(sh);

  UNITTEST_END_SIMPLE
}
