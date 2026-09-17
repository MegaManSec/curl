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

#if !defined(CURL_DISABLE_MIME) &&                                      \
  (!defined(CURL_DISABLE_HTTP) ||                                       \
   !defined(CURL_DISABLE_SMTP) ||                                       \
   !defined(CURL_DISABLE_IMAP))

#define T3600_SENTINEL "t3600-name-that-fails-to-strdup"

struct t3600_ctx {
  int freecount;
};

static int t3600_arm = 0;

static void *t3600_malloc(size_t n)
{
  /* !checksrc! disable BANNEDFUNC 1 */
  return malloc(n);
}

static void t3600_free(void *p)
{
  /* !checksrc! disable BANNEDFUNC 1 */
  free(p);
}

static void *t3600_realloc(void *p, size_t n)
{
  /* !checksrc! disable BANNEDFUNC 1 */
  return realloc(p, n);
}

static void *t3600_calloc(size_t nmemb, size_t size)
{
  /* !checksrc! disable BANNEDFUNC 1 */
  return calloc(nmemb, size);
}

static char *t3600_strdup(const char *str)
{
  if(t3600_arm && str && !strcmp(str, T3600_SENTINEL))
    return NULL;
  return CURLX_STRDUP_LOW(str);
}

static size_t t3600_read_cb(char *buffer, size_t size, size_t nitems,
                            void *arg)
{
  (void)buffer;
  (void)size;
  (void)nitems;
  (void)arg;
  return 0;
}

static void t3600_free_cb(void *arg)
{
  struct t3600_ctx *ctx = (struct t3600_ctx *)arg;

  ctx->freecount++;
}

#endif

static CURLcode test_lib3600(const char *arg)
{
  UNITTEST_BEGIN_SIMPLE

#if !defined(CURL_DISABLE_MIME) &&                                      \
  (!defined(CURL_DISABLE_HTTP) ||                                       \
   !defined(CURL_DISABLE_SMTP) ||                                       \
   !defined(CURL_DISABLE_IMAP))

  CURL *src = NULL;
  CURL *dst = NULL;
  curl_mime *mime = NULL;
  curl_mimepart *part;
  struct t3600_ctx ctx;

  ctx.freecount = 0;

  if(curl_global_init_mem(CURL_GLOBAL_ALL, t3600_malloc, t3600_free,
                          t3600_realloc, t3600_strdup,
                          t3600_calloc) != CURLE_OK) {
    curl_mfprintf(stderr, "curl_global_init_mem() failed\n");
    return TEST_ERR_MAJOR_BAD;
  }

  src = curl_easy_init();
  abort_unless(src != NULL, "curl_easy_init failed");

  mime = curl_mime_init(src);
  abort_unless(mime != NULL, "curl_mime_init failed");
  part = curl_mime_addpart(mime);
  abort_unless(part != NULL, "curl_mime_addpart failed");
  curl_mime_data_cb(part, (curl_off_t)0, t3600_read_cb, NULL,
                    t3600_free_cb, &ctx);
  curl_mime_name(part, T3600_SENTINEL);
  curl_easy_setopt(src, CURLOPT_MIMEPOST, mime);

  /* Force the name duplication further down in Curl_mime_duppart() to
     fail, after the callback part's freefunc/arg have already been
     copied to the destination. */
  t3600_arm = 1;
  dst = curl_easy_duphandle(src);
  t3600_arm = 0;

  fail_unless(dst == NULL, "duplication was expected to fail");
  fail_unless(ctx.freecount == 0,
              "failed duplication must not free source-owned callback "
              "state");

  /* CURLOPT_MIMEPOST does not take ownership of "mime": releasing it
     is what actually triggers the callback part's freefunc. */
  curl_mime_free(mime);

  fail_unless(ctx.freecount == 1,
              "source cleanup must still free its own callback state "
              "exactly once");

  curl_easy_cleanup(dst);
  curl_easy_cleanup(src);
  curl_global_cleanup();
#else
  (void)arg;
#endif

  UNITTEST_END_SIMPLE
}
