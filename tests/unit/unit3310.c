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

/* Unit tests for TLS session cache peer key discrimination on the derived
 * auto_client_cert state (CURLSSLOPT_AUTO_CLIENT_CERT, scoped to the
 * initial origin of a transfer). Verifies that Curl_ssl_peer_key_make()
 * produces distinct keys for otherwise identical configurations that only
 * differ in auto_client_cert, so a cached credential built while it was
 * enabled cannot be looked up by a connection where it is not.
 */

#include "unitcheck.h"
#include "urldata.h"
#include "peer.h"

#ifdef USE_SSL
#include "vtls/vtls.h"
#include "vtls/vtls_scache.h"
#endif

static CURLcode test_unit3310(const char *arg)
{
  UNITTEST_BEGIN_SIMPLE

#ifdef USE_SSL
  struct Curl_peer origin;
  struct ssl_peer peer;
  struct ssl_filter_config ssl;
  char *key1 = NULL;
  char *key2 = NULL;
  static char base_hostname[] = "example.com";

  memset(&origin, 0, sizeof(origin));
  origin.hostname = base_hostname;
  origin.port = 443;

  memset(&peer, 0, sizeof(peer));
  peer.origin = &origin;
  peer.transport = TRNSPRT_TCP;

  memset(&ssl, 0, sizeof(ssl));
  ssl.verifypeer = TRUE;
  ssl.verifyhost = TRUE;

  /* Baseline: same config produces same key. */
  fail_unless(!Curl_ssl_peer_key_make(&peer, &ssl, "test", &key1),
              "peer key build failed");
  fail_unless(!Curl_ssl_peer_key_make(&peer, &ssl, "test", &key2),
              "peer key build failed");
  fail_unless(key1 && key2 && !strcmp(key1, key2),
              "identical config should produce identical peer key");
  curlx_safefree(key1);
  curlx_safefree(key2);

  /* auto_client_cert must produce a different peer key than the same
   * config without it, even with no explicit clientcert set: this is the
   * only way to keep a cached Schannel credential that used automatic
   * client certificate selection from being handed to a connection for
   * which the origin-scoping rule would not have enabled it. */
  fail_unless(!Curl_ssl_peer_key_make(&peer, &ssl, "test", &key1),
              "peer key build failed");
  ssl.auto_client_cert = TRUE;
  fail_unless(!Curl_ssl_peer_key_make(&peer, &ssl, "test", &key2),
              "peer key build failed");
  fail_unless(key1 && key2 && strcmp(key1, key2),
              "auto_client_cert must affect the peer key");
  curlx_safefree(key1);
  curlx_safefree(key2);
  ssl.auto_client_cert = FALSE;
#endif /* USE_SSL */

  UNITTEST_END_SIMPLE
}
