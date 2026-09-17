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

#include "cf-socket.h"

static CURLcode test_unit3313(const char *arg)
{
  UNITTEST_BEGIN_SIMPLE

  fail_unless(Curl_keepalive_ms(0) == 0, "0 seconds should be 0 ms");
  fail_unless(Curl_keepalive_ms(1) == 1000, "1 second should be 1000 ms");
  fail_unless(Curl_keepalive_ms(INT_MAX / 1000) ==
              (INT_MAX / 1000) * 1000,
              "value at the boundary should scale without clamping");
  fail_unless(Curl_keepalive_ms((INT_MAX / 1000) + 1) == INT_MAX,
              "value just above the boundary should clamp to INT_MAX");
  fail_unless(Curl_keepalive_ms(INT_MAX) == INT_MAX,
              "INT_MAX seconds should clamp to INT_MAX ms");

  UNITTEST_END_SIMPLE
}
