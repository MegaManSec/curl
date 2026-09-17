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

#define T1735_MAX_13_LIFETIME_SEC (60 * 60 * 24 * 7)
#define T1735_MAX_12_LIFETIME_SEC (60 * 60 * 24)
#define T1735_SLACK_SEC 60

struct t1735_found {
  char key[64];
  curl_off_t valid_until;
  int ietf_tls_id;
};

struct t1735_ctx {
  struct t1735_found found[4];
  size_t count;
};

static size_t t1735_make_ticket(uint8_t *p, int ietf_id, curl_off_t until)
{
  size_t i;

  p[0] = 0x01; /* CURL_SPACK_VERSION */
  p[1] = 0x04; /* CURL_SPACK_TICKET */
  p[2] = 0x00;
  p[3] = 0x08;
  memset(&p[4], 'T', 8);
  p[12] = 0x02; /* CURL_SPACK_IETF_ID */
  p[13] = (uint8_t)(ietf_id >> 8);
  p[14] = (uint8_t)(ietf_id & 0xff);
  p[15] = 0x03; /* CURL_SPACK_VALID_UNTIL */
  for(i = 0; i < 8; i++)
    p[16 + i] = (uint8_t)(((uint64_t)until) >> (56 - 8 * i));
  return 24;
}

static CURLcode t1735_export_cb(CURL *easy, void *userptr,
                                const char *session_key,
                                const unsigned char *shmac, size_t shmac_len,
                                const unsigned char *sdata, size_t sdata_len,
                                curl_off_t valid_until, int ietf_tls_id,
                                const char *alpn, size_t earlydata_max)
{
  struct t1735_ctx *ctx = userptr;

  (void)easy;
  (void)shmac;
  (void)shmac_len;
  (void)sdata;
  (void)sdata_len;
  (void)alpn;
  (void)earlydata_max;

  if(ctx->count < sizeof(ctx->found) / sizeof(ctx->found[0])) {
    curlx_strcopy(ctx->found[ctx->count].key, sizeof(ctx->found[0].key),
                 session_key, strlen(session_key));
    ctx->found[ctx->count].valid_until = valid_until;
    ctx->found[ctx->count].ietf_tls_id = ietf_tls_id;
    ctx->count++;
  }
  return CURLE_OK;
}

static const struct t1735_found *t1735_find(struct t1735_ctx *ctx,
                                            const char *key)
{
  size_t i;

  for(i = 0; i < ctx->count; i++) {
    if(!strcmp(ctx->found[i].key, key))
      return &ctx->found[i];
  }
  return NULL;
}

static CURLcode test_lib1735(const char *URL)
{
  CURLSH *share = NULL;
  CURL *easy = NULL;
  CURLSHcode shrc;
  CURLcode result = CURLE_FAILED_INIT;
  struct t1735_ctx ctx;
  const struct t1735_found *f;
  curl_off_t now = (curl_off_t)time(NULL);
  uint8_t ticket[24];

  (void)URL;
  memset(&ctx, 0, sizeof(ctx));

  result = curl_global_init(CURL_GLOBAL_ALL);
  if(result != CURLE_OK)
    goto test_cleanup;

  share = curl_share_init();
  easy = curl_easy_init();
  if(!share || !easy)
    goto test_cleanup;

  shrc = curl_share_setopt(share, CURLSHOPT_SHARE,
                           CURL_LOCK_DATA_SSL_SESSION);
  if(shrc != CURLSHE_OK)
    goto test_cleanup;

  result = curl_easy_setopt(easy, CURLOPT_SHARE, share);
  if(result)
    goto test_cleanup;

  t1735_make_ticket(ticket, 0x0304, now + 365 * 86400);
  result = curl_easy_ssls_import(easy, "tls13.example.test:443:h2:G", NULL, 0,
                                 ticket, sizeof(ticket));
  if(result) {
    curl_mfprintf(stderr, "import tls13 ticket failed: %d\n", (int)result);
    goto test_cleanup;
  }

  t1735_make_ticket(ticket, 0x0303, now + 365 * 86400);
  result = curl_easy_ssls_import(easy, "tls12.example.test:443:h2:G", NULL, 0,
                                 ticket, sizeof(ticket));
  if(result) {
    curl_mfprintf(stderr, "import tls12 ticket failed: %d\n", (int)result);
    goto test_cleanup;
  }

  t1735_make_ticket(ticket, 0x0304, now - 3600);
  result = curl_easy_ssls_import(easy, "expired.example.test:443:h2:G",
                                 NULL, 0, ticket, sizeof(ticket));
  if(result) {
    curl_mfprintf(stderr, "import expired ticket failed: %d\n",
                 (int)result);
    goto test_cleanup;
  }

  result = curl_easy_ssls_export(easy, t1735_export_cb, &ctx);
  if(result) {
    curl_mfprintf(stderr, "export failed: %d\n", (int)result);
    goto test_cleanup;
  }

  result = CURLE_FAILED_INIT;

  f = t1735_find(&ctx, "expired.example.test:443:h2:G");
  if(f) {
    curl_mfprintf(stderr, "expired ticket was not discarded on import\n");
    goto test_cleanup;
  }

  f = t1735_find(&ctx, "tls13.example.test:443:h2:G");
  if(!f) {
    curl_mfprintf(stderr, "tls13 ticket missing from export\n");
    goto test_cleanup;
  }
  if(f->valid_until > now + T1735_MAX_13_LIFETIME_SEC + T1735_SLACK_SEC) {
    curl_mfprintf(stderr, "tls13 ticket lifetime not clamped: "
                 "valid_until=%" FMT_OFF_T ", now=%" FMT_OFF_T "\n",
                 f->valid_until, now);
    goto test_cleanup;
  }

  f = t1735_find(&ctx, "tls12.example.test:443:h2:G");
  if(!f) {
    curl_mfprintf(stderr, "tls12 ticket missing from export\n");
    goto test_cleanup;
  }
  if(f->valid_until > now + T1735_MAX_12_LIFETIME_SEC + T1735_SLACK_SEC) {
    curl_mfprintf(stderr, "tls12 ticket lifetime not clamped: "
                 "valid_until=%" FMT_OFF_T ", now=%" FMT_OFF_T "\n",
                 f->valid_until, now);
    goto test_cleanup;
  }

  result = CURLE_OK;

test_cleanup:
  curl_easy_cleanup(easy);
  curl_share_cleanup(share);
  curl_global_cleanup();

  return result;
}
#else
static CURLcode test_lib1735(const char *URL)
{
  (void)URL;
  return CURLE_OK;
}
#endif /* USE_SSL && USE_SSLS_EXPORT */
