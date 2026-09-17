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

/* Unit tests for the SSLKEYLOGFILE handling in vtls/keylog.c. Several
 * threads hammer Curl_tls_keylog_open()/_close()/_write()/_write_line()
 * at the same time, which reproduces the concurrent open/close/write
 * access pattern that used to race on the shared keylog_file_fp. Build
 * with -fsanitize=thread to have TSan confirm there is no data race.
 */

#include "unitcheck.h"
#include "curl_threads.h"
#include "vtls/keylog.h"

#if defined(USE_THREADS) && \
  (defined(USE_OPENSSL) || defined(USE_GNUTLS) || defined(USE_WOLFSSL) || \
   defined(USE_RUSTLS))

#define NUM_OPEN_THREADS  4
#define NUM_WRITE_THREADS 4
#define NUM_CLOSE_THREADS 2
#define NUM_ITERATIONS    200

static const unsigned char unit3312_random[32] = {
  0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
  0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
  0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
  0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20
};

static const unsigned char unit3312_secret[16] = {
  0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x11, 0x22,
  0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0x00
};

static CURL_THREAD_RETURN_T CURL_STDCALL unit3312_open_thread(void *arg)
{
  int i;
  (void)arg;
  for(i = 0; i < NUM_ITERATIONS; i++) {
    Curl_tls_keylog_open();
  }
  return 0;
}

static CURL_THREAD_RETURN_T CURL_STDCALL unit3312_write_thread(void *arg)
{
  int i;
  (void)arg;
  for(i = 0; i < NUM_ITERATIONS; i++) {
    Curl_tls_keylog_write("CLIENT_RANDOM", unit3312_random,
                          sizeof(unit3312_random),
                          unit3312_secret, sizeof(unit3312_secret));
    Curl_tls_keylog_write_line("CLIENT_RANDOM "
                               "0102030405060708090a0b0c0d0e0f10"
                               "1112131415161718191a1b1c1d1e1f20 "
                               "aabbccddeeff112233445566778899");
    Curl_tls_keylog_enabled();
  }
  return 0;
}

static CURL_THREAD_RETURN_T CURL_STDCALL unit3312_close_thread(void *arg)
{
  int i;
  (void)arg;
  for(i = 0; i < NUM_ITERATIONS; i++) {
    Curl_tls_keylog_close();
    Curl_tls_keylog_open();
  }
  return 0;
}

static void unit3312_setenv(const char *value)
{
#ifdef _WIN32
  _putenv_s("SSLKEYLOGFILE", value);
#else
  setenv("SSLKEYLOGFILE", value, 1);
#endif
}

static void unit3312_unsetenv(void)
{
#ifdef _WIN32
  _putenv_s("SSLKEYLOGFILE", "");
#else
  unsetenv("SSLKEYLOGFILE");
#endif
}

static CURLcode test_unit3312(const char *arg)
{
  UNITTEST_BEGIN_SIMPLE
  curl_thread_t open_th[NUM_OPEN_THREADS];
  curl_thread_t write_th[NUM_WRITE_THREADS];
  curl_thread_t close_th[NUM_CLOSE_THREADS];
  int i;

  /* start from a known, closed state */
  Curl_tls_keylog_close();
  unit3312_setenv(arg);

  for(i = 0; i < NUM_OPEN_THREADS; i++)
    open_th[i] = Curl_thread_create(unit3312_open_thread, NULL);
  for(i = 0; i < NUM_WRITE_THREADS; i++)
    write_th[i] = Curl_thread_create(unit3312_write_thread, NULL);
  for(i = 0; i < NUM_CLOSE_THREADS; i++)
    close_th[i] = Curl_thread_create(unit3312_close_thread, NULL);

  for(i = 0; i < NUM_OPEN_THREADS; i++) {
    if(open_th[i] != curl_thread_t_null)
      Curl_thread_join(&open_th[i]);
  }
  for(i = 0; i < NUM_WRITE_THREADS; i++) {
    if(write_th[i] != curl_thread_t_null)
      Curl_thread_join(&write_th[i]);
  }
  for(i = 0; i < NUM_CLOSE_THREADS; i++) {
    if(close_th[i] != curl_thread_t_null)
      Curl_thread_join(&close_th[i]);
  }

  /* the module must still be in a well-defined, usable state */
  Curl_tls_keylog_open();
  fail_unless(Curl_tls_keylog_enabled(), "keylog file should be open");
  fail_unless(Curl_tls_keylog_write_line("CLIENT_RANDOM final line"),
              "write after the thread storm should succeed");

  Curl_tls_keylog_close();
  fail_unless(!Curl_tls_keylog_enabled(),
              "keylog file should be closed");

  unit3312_unsetenv();

  UNITTEST_END_SIMPLE
}

#else
static CURLcode test_unit3312(const char *arg)
{
  UNITTEST_BEGIN_SIMPLE
  (void)arg;
  UNITTEST_END_SIMPLE
}
#endif
