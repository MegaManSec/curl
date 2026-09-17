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

#include "socketpair.h"

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#ifndef INADDR_LOOPBACK
#define INADDR_LOOPBACK 0x7f000001
#endif

/* This exercises the nonce-verification loop that Curl_wakeup_init() uses
   on platforms without eventfd, pipe or socketpair (verify_nonce() in
   socketpair.c). A local peer that connects to the loopback listener and
   then silently holds the connection open, never sending anything and
   never closing, must still make the handshake fail once the internal
   deadline elapses, not hang forever. */

static CURLcode test_unit1963(const char *arg)
{
  UNITTEST_BEGIN_SIMPLE

  curl_socket_t listener = CURL_SOCKET_BAD;
  curl_socket_t peer = CURL_SOCKET_BAD;
  curl_socket_t accepted = CURL_SOCKET_BAD;
  curl_socket_t writer = CURL_SOCKET_BAD;
  union {
    struct sockaddr_in inaddr;
    struct sockaddr addr;
  } a;
  curl_socklen_t addrlen = sizeof(a.inaddr);
  struct curltime start;
  timediff_t elapsed_ms;
  int rc;

  listener = CURL_SOCKET(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  abort_if(listener == CURL_SOCKET_BAD, "failed to create listener socket");

  memset(&a, 0, sizeof(a));
  a.inaddr.sin_family = AF_INET;
  a.inaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  a.inaddr.sin_port = 0;

  abort_if(bind(listener, &a.addr, sizeof(a.inaddr)) == -1,
           "failed to bind listener socket");
  abort_if(getsockname(listener, &a.addr, &addrlen) == -1,
           "failed to get listener address");
  abort_if(listen(listener, 2) == -1, "failed to listen");

  /* the "attacker": connects and is about to be accepted, then holds the
     connection open silently, never sending data and never closing */
  peer = CURL_SOCKET(AF_INET, SOCK_STREAM, 0);
  abort_if(peer == CURL_SOCKET_BAD, "failed to create peer socket");
  abort_if(connect(peer, &a.addr, sizeof(a.inaddr)) == -1,
           "failed to connect peer socket");

  accepted = CURL_ACCEPT(listener, NULL, NULL);
  abort_if(accepted == CURL_SOCKET_BAD, "failed to accept peer connection");

  /* an unrelated, still-open socket to use as the write side, so the
     nonce write itself cannot fail with a broken pipe */
  writer = CURL_SOCKET(AF_INET, SOCK_STREAM, 0);
  abort_if(writer == CURL_SOCKET_BAD, "failed to create writer socket");
  abort_if(connect(writer, &a.addr, sizeof(a.inaddr)) == -1,
           "failed to connect writer socket");

  start = curlx_now();
  rc = verify_nonce(writer, accepted);
  elapsed_ms = curlx_timediff_ms(curlx_now(), start);

  fail_unless(rc != 0, "verify_nonce must fail on a silent peer");
  fail_if(elapsed_ms > 65000,
          "verify_nonce took too long, it must give up around its "
          "internal 60 second deadline instead of hanging forever");

  sclose(peer);
  sclose(writer);
  sclose(accepted);
  sclose(listener);

  UNITTEST_END_SIMPLE
}
