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

#if defined(USE_SSL) && defined(USE_SSLS_EXPORT)

#define T1691_TICKET_SIZE 16

struct t1691_capture {
  uint8_t *sdata;
  size_t sdata_len;
  int calls;
};

static uint8_t *t1691_make_packet(bool with_sectrust, size_t *packet_len)
{
  const size_t pkt_len =
    1 +                          /* format version */
    1 + 2 + T1691_TICKET_SIZE +  /* ticket */
    1 + 2 +                      /* IETF TLS id */
    (with_sectrust ? 1 : 0);     /* forged sectrust tag */
  uint8_t *packet = curlx_malloc(pkt_len);
  uint8_t *p;

  if(!packet)
    return NULL;

  p = packet;
  *p++ = 0x01;                     /* CURL_SPACK_VERSION */
  *p++ = 0x04;                     /* CURL_SPACK_TICKET */
  *p++ = (uint8_t)(T1691_TICKET_SIZE >> 8);
  *p++ = (uint8_t)(T1691_TICKET_SIZE & 0x0ff);
  memset(p, 'T', T1691_TICKET_SIZE);
  p += T1691_TICKET_SIZE;
  *p++ = 0x02;                     /* CURL_SPACK_IETF_ID */
  *p++ = 0x03;
  *p++ = 0x04;
  if(with_sectrust)
    *p++ = 0x08;                   /* CURL_SPACK_SECTRUST, forged */

  *packet_len = pkt_len;
  return packet;
}

static CURLcode t1691_export_cb(CURL *handle, void *userptr,
                                const char *session_key,
                                const unsigned char *shmac, size_t shmac_len,
                                const unsigned char *sdata, size_t sdata_len,
                                curl_off_t valid_until, int ietf_tls_id,
                                const char *alpn, size_t earlydata_max)
{
  struct t1691_capture *cap = userptr;
  (void)handle;
  (void)session_key;
  (void)shmac;
  (void)shmac_len;
  (void)valid_until;
  (void)ietf_tls_id;
  (void)alpn;
  (void)earlydata_max;

  cap->calls++;
  curlx_free(cap->sdata);
  cap->sdata = curlx_malloc(sdata_len);
  if(!cap->sdata)
    return CURLE_OUT_OF_MEMORY;
  memcpy(cap->sdata, sdata, sdata_len);
  cap->sdata_len = sdata_len;
  return CURLE_OK;
}

static CURLcode t1691_import_export(bool with_sectrust,
                                    struct t1691_capture *cap)
{
  uint8_t *packet;
  size_t packet_len;
  unsigned char shmac[64];
  CURLSH *share = NULL;
  CURL *easy = NULL;
  CURLSHcode shrc;
  CURLcode result = CURLE_FAILED_INIT;
  size_t i;

  memset(cap, 0, sizeof(*cap));
  packet = t1691_make_packet(with_sectrust, &packet_len);
  if(!packet)
    return CURLE_OUT_OF_MEMORY;

  for(i = 0; i < sizeof(shmac); i++)
    shmac[i] = (unsigned char)(0xA0 + i);

  share = curl_share_init();
  easy = curl_easy_init();
  if(!share || !easy)
    goto cleanup;

  shrc = curl_share_setopt(share, CURLSHOPT_SHARE,
                           CURL_LOCK_DATA_SSL_SESSION);
  if(shrc != CURLSHE_OK)
    goto cleanup;

  result = curl_easy_setopt(easy, CURLOPT_SHARE, share);
  if(result)
    goto cleanup;

  result = curl_easy_ssls_import(easy, NULL, shmac, sizeof(shmac),
                                 packet, packet_len);
  if(result) {
    curl_mfprintf(stderr, "import failed: %d (%s)\n",
                  (int)result, curl_easy_strerror(result));
    goto cleanup;
  }

  result = curl_easy_ssls_export(easy, t1691_export_cb, cap);
  if(result) {
    curl_mfprintf(stderr, "export failed: %d (%s)\n",
                  (int)result, curl_easy_strerror(result));
    goto cleanup;
  }

cleanup:
  curlx_free(packet);
  curl_easy_cleanup(easy);
  curl_share_cleanup(share);
  return result;
}

static CURLcode test_lib1691(const char *URL)
{
  struct t1691_capture control, exploit;
  CURLcode result;

  (void)URL;
  result = curl_global_init(CURL_GLOBAL_ALL);
  if(result != CURLE_OK)
    return result;

  result = t1691_import_export(FALSE, &control);
  if(result)
    goto test_cleanup;

  result = t1691_import_export(TRUE, &exploit);
  if(result)
    goto test_cleanup;

  if(control.calls != 1 || exploit.calls != 1) {
    curl_mfprintf(stderr, "expected exactly one exported session in each "
                  "run, got %d and %d\n", control.calls, exploit.calls);
    result = CURLE_FAILED_INIT;
    goto test_cleanup;
  }

  /* forged CURL_SPACK_SECTRUST tag must not survive import */
  if(exploit.sdata_len != control.sdata_len ||
     memcmp(exploit.sdata, control.sdata, control.sdata_len)) {
    curl_mfprintf(stderr, "forged sectrust tag survived import/export "
                  "round-trip: control %zu bytes, exploit %zu bytes\n",
                  control.sdata_len, exploit.sdata_len);
    result = CURLE_FAILED_INIT;
    goto test_cleanup;
  }

test_cleanup:
  curlx_free(control.sdata);
  curlx_free(exploit.sdata);
  curl_global_cleanup();
  return result;
}
#else
static CURLcode test_lib1691(const char *URL)
{
  (void)URL;
  return CURLE_OK;
}
#endif /* USE_SSL && USE_SSLS_EXPORT */
