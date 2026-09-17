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
#include "curl_setup.h"

#ifndef CURL_DISABLE_WEBSOCKETS

#include "curl_sha1.h"

/* This is the SHA1 algorithm as specified in RFC 3174. It is only used to
 * compute the WebSocket 'Sec-WebSocket-Accept' handshake value (RFC 6455),
 * which mandates SHA1 regardless of TLS backend (or its absence, since
 * "ws://" is used without TLS at all), so this implementation does not
 * hook into any of the TLS backend crypto libraries.
 */

#define WPA_GET_BE32(a)               \
  ((((unsigned long)(a)[0]) << 24) |  \
   (((unsigned long)(a)[1]) << 16) |  \
   (((unsigned long)(a)[2]) <<  8) |  \
    ((unsigned long)(a)[3]))
#define WPA_PUT_BE32(a, val)                                          \
  do {                                                                \
    (a)[0] = (unsigned char)((((unsigned long)(val)) >> 24) & 0xff);  \
    (a)[1] = (unsigned char)((((unsigned long)(val)) >> 16) & 0xff);  \
    (a)[2] = (unsigned char)((((unsigned long)(val)) >>  8) & 0xff);  \
    (a)[3] = (unsigned char)(((unsigned long)(val)) & 0xff);          \
  } while(0)

#define WPA_PUT_BE64(a, val)                             \
  do {                                                    \
    (a)[0] = (unsigned char)(((uint64_t)(val)) >> 56);   \
    (a)[1] = (unsigned char)(((uint64_t)(val)) >> 48);   \
    (a)[2] = (unsigned char)(((uint64_t)(val)) >> 40);   \
    (a)[3] = (unsigned char)(((uint64_t)(val)) >> 32);   \
    (a)[4] = (unsigned char)(((uint64_t)(val)) >> 24);   \
    (a)[5] = (unsigned char)(((uint64_t)(val)) >> 16);   \
    (a)[6] = (unsigned char)(((uint64_t)(val)) >>  8);   \
    (a)[7] = (unsigned char)(((uint64_t)(val)) & 0xff);  \
  } while(0)

#define CURL_SHA1_BLOCK_SIZE 64

struct sha1_state {
  uint64_t length;
  unsigned long state[5], curlen;
  unsigned char buf[CURL_SHA1_BLOCK_SIZE];
};

#define Sha1_Rol(x, n) \
  ((((unsigned long)(x) << (n)) | \
    (((unsigned long)(x) & 0xFFFFFFFFUL) >> (32 - (n)))) & 0xFFFFFFFFUL)

static int sha1_compress(struct sha1_state *md, const unsigned char *buf)
{
  unsigned long a, b, c, d, e, w[80];
  int i;

  for(i = 0; i < 16; i++)
    w[i] = WPA_GET_BE32(buf + (4 * i));
  for(i = 16; i < 80; i++)
    w[i] = Sha1_Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

  a = md->state[0];
  b = md->state[1];
  c = md->state[2];
  d = md->state[3];
  e = md->state[4];

  for(i = 0; i < 80; i++) {
    unsigned long f, k, t;
    if(i < 20) {
      f = (b & c) | ((~b) & d);
      k = 0x5A827999UL;
    }
    else if(i < 40) {
      f = b ^ c ^ d;
      k = 0x6ED9EBA1UL;
    }
    else if(i < 60) {
      f = (b & c) | (b & d) | (c & d);
      k = 0x8F1BBCDCUL;
    }
    else {
      f = b ^ c ^ d;
      k = 0xCA62C1D6UL;
    }
    t = (Sha1_Rol(a, 5) + f + e + k + w[i]) & 0xFFFFFFFFUL;
    e = d;
    d = c;
    c = Sha1_Rol(b, 30);
    b = a;
    a = t;
  }

  md->state[0] += a;
  md->state[1] += b;
  md->state[2] += c;
  md->state[3] += d;
  md->state[4] += e;

  return 0;
}

static CURLcode sha1_init(struct sha1_state *md)
{
  md->curlen = 0;
  md->length = 0;
  md->state[0] = 0x67452301UL;
  md->state[1] = 0xEFCDAB89UL;
  md->state[2] = 0x98BADCFEUL;
  md->state[3] = 0x10325476UL;
  md->state[4] = 0xC3D2E1F0UL;
  return CURLE_OK;
}

static void sha1_update(struct sha1_state *md,
                        const unsigned char *in, unsigned int len)
{
  unsigned long inlen = len;
  unsigned long n;

  if(md->curlen > sizeof(md->buf))
    return;
  while(inlen > 0) {
    if(md->curlen == 0 && inlen >= CURL_SHA1_BLOCK_SIZE) {
      if(sha1_compress(md, in) < 0)
        return;
      md->length += CURL_SHA1_BLOCK_SIZE * 8;
      in += CURL_SHA1_BLOCK_SIZE;
      inlen -= CURL_SHA1_BLOCK_SIZE;
    }
    else {
      n = CURLMIN(inlen, (CURL_SHA1_BLOCK_SIZE - md->curlen));
      memcpy(md->buf + md->curlen, in, n);
      md->curlen += n;
      in += n;
      inlen -= n;
      if(md->curlen == CURL_SHA1_BLOCK_SIZE) {
        if(sha1_compress(md, md->buf) < 0)
          return;
        md->length += 8 * CURL_SHA1_BLOCK_SIZE;
        md->curlen = 0;
      }
    }
  }
}

static void sha1_final(unsigned char *out, struct sha1_state *md)
{
  int i;

  if(md->curlen >= sizeof(md->buf))
    return;

  md->length += md->curlen * 8;
  md->buf[md->curlen++] = (unsigned char)0x80;

  if(md->curlen > 56) {
    while(md->curlen < 64)
      md->buf[md->curlen++] = 0;
    sha1_compress(md, md->buf);
    md->curlen = 0;
  }

  while(md->curlen < 56)
    md->buf[md->curlen++] = 0;

  WPA_PUT_BE64(md->buf + 56, md->length);
  sha1_compress(md, md->buf);

  for(i = 0; i < 5; i++)
    WPA_PUT_BE32(out + (4 * i), md->state[i]);
}

/*
 * Curl_sha1it()
 *
 * Generates a SHA1 hash for the given input data.
 *
 * Parameters:
 *
 * output [in/out] - The output buffer.
 * input  [in]     - The input data.
 * length [in]     - The input length.
 *
 * Returns CURLE_OK on success.
 */
CURLcode Curl_sha1it(unsigned char *output, const unsigned char *input,
                     size_t len)
{
  struct sha1_state ctx;
  CURLcode result = sha1_init(&ctx);
  if(!result) {
    do {
      unsigned int ilen = (unsigned int)CURLMIN(len, UINT_MAX);
      sha1_update(&ctx, input, ilen);
      len -= ilen;
      input += ilen;
    } while(len);
    sha1_final(output, &ctx);
  }
  return result;
}

#endif /* !CURL_DISABLE_WEBSOCKETS */
