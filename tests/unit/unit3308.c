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

/* Unit tests for TLS session cache peer key discrimination on CAfile
 * content. Verifies that Curl_ssl_peer_key_build() produces a different key
 * when the file at a CAfile path is replaced, so a previously cached TLS
 * session established under the old trust material is not resumed under
 * the new one.
 */

#include "unitcheck.h"
#include "urldata.h"
#include "peer.h"

#ifdef USE_SSL
#include "vtls/vtls.h"
#include "vtls/vtls_scache.h"
#endif

static CURLcode writefile(const char *path, const char *content)
{
  FILE *fp = curlx_fopen(path, "wb");
  if(!fp)
    return CURLE_WRITE_ERROR;
  fwrite(content, 1, strlen(content), fp);
  curlx_fclose(fp);
  return CURLE_OK;
}

static CURLcode test_unit3308(const char *arg)
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
  ssl.CAfile = CURL_UNCONST(arg);

  /* Baseline: an unchanged CAfile produces the same key on repeat calls. */
  fail_unless(!writefile(arg, "-----BEGIN CERTIFICATE-----AAAA"),
              "could not write CAfile");
  fail_unless(!Curl_ssl_peer_key_make(&peer, &ssl, "test", &key1),
              "peer key build failed");
  fail_unless(!Curl_ssl_peer_key_make(&peer, &ssl, "test", &key2),
              "peer key build failed");
  fail_unless(key1 && key2 && !strcmp(key1, key2),
              "unchanged CAfile should produce identical peer key");
  curlx_safefree(key2);

  /* Replacing the CAfile content at the same path must change the key, so
   * a session cached under the old trust material is not resumed. */
  fail_unless(!writefile(arg, "-----BEGIN CERTIFICATE-----BBBBBBBBBBBBBBBB"),
              "could not rewrite CAfile");
  fail_unless(!Curl_ssl_peer_key_make(&peer, &ssl, "test", &key2),
              "peer key build failed");
  fail_unless(key1 && key2 && strcmp(key1, key2),
              "replacing the CAfile content must change the peer key");
  curlx_safefree(key1);
  curlx_safefree(key2);

  /* A CAfile that does not exist must not crash key generation. */
  ssl.CAfile = CURL_UNCONST("/nonexistent/path/to/ca-does-not-exist.pem");
  fail_unless(!Curl_ssl_peer_key_make(&peer, &ssl, "test", &key1),
              "peer key build failed for a missing CAfile");
  fail_unless(key1 != NULL, "peer key should still be produced");
  curlx_safefree(key1);
#else
  (void)arg;
#endif /* USE_SSL */

  UNITTEST_END_SIMPLE
}
