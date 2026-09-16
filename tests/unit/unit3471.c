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
#include "setopt.h"
#include "url.h"
#include "uint-hashset.h"
#include "bufref.h"

/* Watches one pointer's content and, when it is freed, records whether it
 * was all-zero at that point. The real free is always chained to, so no
 * memory is ever inspected after it was actually released. */
static const char *t3471_watch_ptr;
static size_t t3471_watch_len;
static int t3471_watch_zeroed;
static curl_free_callback t3471_real_cfree;

static void t3471_free_watch(void *ptr)
{
  if(t3471_watch_ptr) {
    size_t i;
    t3471_watch_zeroed = 1;
    for(i = 0; i < t3471_watch_len; ++i) {
      if(t3471_watch_ptr[i]) {
        t3471_watch_zeroed = 0;
        break;
      }
    }
    t3471_watch_ptr = NULL;
  }
  t3471_real_cfree(ptr);
}

static void t3471_watch_start(const void *ptr, size_t len)
{
  t3471_watch_ptr = ptr;
  t3471_watch_len = len;
  t3471_watch_zeroed = -1;
  t3471_real_cfree = Curl_cfree;
  Curl_cfree = t3471_free_watch;
}

static int t3471_watch_stop(void)
{
  Curl_cfree = t3471_real_cfree;
  return t3471_watch_zeroed;
}

static void t3471_check_setblobopt_zero(void)
{
  struct curl_blob first;
  struct curl_blob second;
  struct curl_blob *blobp = NULL;
  char secret1[] = "topsecret-private-key-1";
  char secret2[] = "replacement-private-key-2";
  CURLcode result;

  first.data = secret1;
  first.len = strlen(secret1);
  first.flags = CURL_BLOB_COPY;
  result = Curl_setblobopt(&blobp, &first);
  fail_unless(!result, "blob-set1 failed");
  fail_unless(blobp && blobp->data, "blob-set1 produced no data");

  /* replace: Curl_setblobopt() must zero the old copy before free */
  t3471_watch_start(blobp->data, blobp->len);
  second.data = secret2;
  second.len = strlen(secret2);
  second.flags = CURL_BLOB_COPY;
  result = Curl_setblobopt(&blobp, &second);
  fail_unless(!result, "blob-replace failed");
  fail_unless(t3471_watch_stop() == 1, "replaced blob secret not zeroed");

  /* clear: Curl_setblobopt() must zero the last copy before free */
  t3471_watch_start(blobp->data, blobp->len);
  result = Curl_setblobopt(&blobp, NULL);
  fail_unless(!result, "blob-clear failed");
  fail_unless(!blobp, "blobp not NULL after clear");
  fail_unless(t3471_watch_stop() == 1, "cleared blob secret not zeroed");
}

static void t3471_check_freeset_zero(void)
{
  struct Curl_easy data;
  struct curl_blob key;
  char secret[] = "topsecret-private-key-3";
  CURLcode result;

  memset(&data, 0, sizeof(data));
  Curl_u8_strset_init(&data.set.strings);
  Curl_bufref_init(&data.state.referer);
  Curl_bufref_init(&data.state.url);

  key.data = secret;
  key.len = strlen(secret);
  key.flags = CURL_BLOB_COPY;
  result = Curl_setblobopt(&data.set.blobs[BLOB_KEY], &key);
  fail_unless(!result, "blob-set-key failed");
  fail_unless(data.set.blobs[BLOB_KEY] && data.set.blobs[BLOB_KEY]->data,
              "blob-set-key produced no data");

  /* Curl_freeset() must zero the blob before free */
  t3471_watch_start(data.set.blobs[BLOB_KEY]->data,
                    data.set.blobs[BLOB_KEY]->len);
  Curl_freeset(&data);
  fail_unless(t3471_watch_stop() == 1, "freeset did not zero blob secret");
}

static CURLcode test_unit3471(const char *arg)
{
  UNITTEST_BEGIN_SIMPLE

  t3471_check_setblobopt_zero();
  t3471_check_freeset_zero();

  UNITTEST_END_SIMPLE
}
