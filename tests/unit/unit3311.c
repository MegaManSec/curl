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

#if defined(USE_SSL) && defined(USE_SSLS_EXPORT)
#include "vtls/vtls_scache.h"
#include "vtls/vtls_spack.h"
#include "vtls/vtls.h"
#endif

#if defined(USE_SSL) && defined(USE_SSLS_EXPORT)

static CURLM *unit3311_multi;
static int unit3311_calls;
static CURLMcode unit3311_remove_mresult;

static CURLcode unit3311_export_remove(CURL *handle, void *userptr,
                                       const char *session_key,
                                       const unsigned char *shmac,
                                       size_t shmac_len,
                                       const unsigned char *sdata,
                                       size_t sdata_len,
                                       curl_off_t valid_until,
                                       int ietf_tls_id, const char *alpn,
                                       size_t earlydata_max)
{
  (void)userptr; (void)session_key; (void)shmac; (void)shmac_len;
  (void)sdata; (void)sdata_len; (void)valid_until; (void)ietf_tls_id;
  (void)alpn; (void)earlydata_max;
  unit3311_calls++;
  unit3311_remove_mresult = curl_multi_remove_handle(unit3311_multi, handle);
  return CURLE_OK;
}

static CURLcode unit3311_export_detach(CURL *handle, void *userptr,
                                       const char *session_key,
                                       const unsigned char *shmac,
                                       size_t shmac_len,
                                       const unsigned char *sdata,
                                       size_t sdata_len,
                                       curl_off_t valid_until,
                                       int ietf_tls_id, const char *alpn,
                                       size_t earlydata_max)
{
  struct Curl_easy *data = handle;
  (void)userptr; (void)session_key; (void)shmac; (void)shmac_len;
  (void)sdata; (void)sdata_len; (void)valid_until; (void)ietf_tls_id;
  (void)alpn; (void)earlydata_max;
  unit3311_calls++;
  data->multi = NULL;
  return CURLE_OK;
}

static CURLcode unit3311_add_ticket(struct Curl_easy *data)
{
  struct Curl_ssl_session *s = NULL;
  struct dynbuf packed;
  CURLcode result;
  void *sdata_copy = curlx_strdup("unit3311-ticket");

  if(!sdata_copy)
    return CURLE_OUT_OF_MEMORY;

  result = Curl_ssl_session_create(sdata_copy, strlen("unit3311-ticket"),
                                   CURL_IETF_PROTO_TLS1_3, NULL, 0, 0, &s);
  if(result)
    return result;

  curlx_dyn_init(&packed, 4096);
  result = Curl_ssl_session_pack(data, s, &packed);
  Curl_ssl_session_destroy(s);
  if(result) {
    curlx_dyn_free(&packed);
    return result;
  }

  result = Curl_ssl_session_import(data, "unit3311.example:443:G", NULL, 0,
                                   curlx_dyn_uptr(&packed),
                                   curlx_dyn_len(&packed));
  curlx_dyn_free(&packed);
  return result;
}

#endif /* USE_SSL && USE_SSLS_EXPORT */

static CURLcode test_unit3311(const char *arg)
{
  UNITTEST_BEGIN_SIMPLE

#if defined(USE_SSL) && defined(USE_SSLS_EXPORT)
  CURLM *multi = NULL;
  struct Curl_easy *easy1 = NULL;
  struct Curl_easy *easy2 = NULL;
  CURLcode result;

  curl_global_init(CURL_GLOBAL_ALL);
  multi = curl_multi_init();
  abort_if(!multi, "multi_init failed");
  easy1 = curl_easy_init();
  easy2 = curl_easy_init();
  abort_if(!easy1 || !easy2, "easy_init failed");

  fail_unless(curl_multi_add_handle(multi, easy1) == CURLM_OK,
              "multi_add_handle easy1 failed");
  fail_unless(curl_multi_add_handle(multi, easy2) == CURLM_OK,
              "multi_add_handle easy2 failed");

  result = unit3311_add_ticket(easy1);
  fail_unless(result == CURLE_OK, "adding an exportable ticket failed");

  /* An export_fn that removes its own easy handle from the multi it is
   * exporting from must be rejected: the scache lock guard must not
   * allow curl_multi_remove_handle() to run reentrantly. */
  unit3311_multi = multi;
  unit3311_calls = 0;
  unit3311_remove_mresult = CURLM_OK;
  result = Curl_ssl_session_export(easy1, unit3311_export_remove, NULL);
  fail_unless(result == CURLE_OK, "export failed");
  fail_unless(unit3311_calls == 1, "export callback should run once");
  fail_unless(unit3311_remove_mresult == CURLM_RECURSIVE_API_CALL,
              "multi_remove_handle from within export_fn must be rejected");
  fail_unless(easy1->multi == multi,
              "easy1 must still be attached to its multi handle");
  fail_unless(!Curl_ssl_scache_is_locked_by_current_thread(easy2),
              "scache must not stay locked after export returns");

  /* Even if data->multi still went away during export_fn (for whatever
   * reason), the scache that export locked must still get unlocked. */
  unit3311_calls = 0;
  result = Curl_ssl_session_export(easy1, unit3311_export_detach, NULL);
  fail_unless(result == CURLE_OK, "export failed");
  fail_unless(unit3311_calls == 1, "export callback should run once");
  fail_unless(!Curl_ssl_scache_is_locked_by_current_thread(easy2),
              "scache must not stay locked when data->multi went away "
              "during export_fn");

  /* restore bookkeeping consistency for a clean shutdown */
  easy1->multi = multi;
  curl_multi_remove_handle(multi, easy1);
  curl_multi_remove_handle(multi, easy2);
  curl_easy_cleanup(easy1);
  curl_easy_cleanup(easy2);
  curl_multi_cleanup(multi);
  curl_global_cleanup();
#endif

  UNITTEST_END_SIMPLE
}
