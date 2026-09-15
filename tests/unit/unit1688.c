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

#if defined(HAVE_GETIFADDRS) && \
  (!defined(CURL_DISABLE_BINDLOCAL) || !defined(CURL_DISABLE_FTP))
#ifdef HAVE_IFADDRS_H
#include <ifaddrs.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#include "if2ip.h"

/* a family value that is guaranteed to differ from AF_INET, used to
   exercise the "interface exists but wrong address family" path */
#define FAKE_OTHER_AF (AF_INET + 1)

static CURLcode test_unit1688(const char *arg)
{
  UNITTEST_BEGIN_SIMPLE

  struct sockaddr_in sin_eth0;
  struct sockaddr other_family;
  struct ifaddrs wlan0;
  struct ifaddrs eth0;
  char wlan0_name[] = "wlan0";
  char eth0_name[] = "eth0";
  char buf[128];
  if2ip_result_t res;

  memset(&sin_eth0, 0, sizeof(sin_eth0));
  sin_eth0.sin_family = AF_INET;
  fail_unless(curlx_inet_pton(AF_INET, "203.0.113.5",
                              &sin_eth0.sin_addr) == 1,
              "inet_pton setup failed");

  memset(&other_family, 0, sizeof(other_family));
  other_family.sa_family = FAKE_OTHER_AF;

  /* linked list: wlan0 (other af) -> eth0 (AF_INET, 203.0.113.5) */
  memset(&wlan0, 0, sizeof(wlan0));
  wlan0.ifa_name = wlan0_name;
  wlan0.ifa_addr = &other_family;
  wlan0.ifa_next = &eth0;

  memset(&eth0, 0, sizeof(eth0));
  eth0.ifa_name = eth0_name;
  eth0.ifa_addr = (struct sockaddr *)&sin_eth0;
  eth0.ifa_next = NULL;

  /* exact case match finds the interface */
  buf[0] = '\0';
  res = if2ip_scan(&wlan0, AF_INET,
#ifdef USE_IPV6
                   IPV6_SCOPE_GLOBAL, 0,
#endif
                   "eth0", buf, sizeof(buf));
  fail_unless(res == IF2IP_FOUND, "exact case match should be found");
  fail_unless(!strcmp(buf, "203.0.113.5"), "wrong address returned");

  /* a differently-cased name must NOT match a real interface */
  buf[0] = '\0';
  res = if2ip_scan(&wlan0, AF_INET,
#ifdef USE_IPV6
                   IPV6_SCOPE_GLOBAL, 0,
#endif
                   "Eth0", buf, sizeof(buf));
  fail_unless(res == IF2IP_NOT_FOUND,
              "case-mismatched interface name must not match");

  buf[0] = '\0';
  res = if2ip_scan(&wlan0, AF_INET,
#ifdef USE_IPV6
                   IPV6_SCOPE_GLOBAL, 0,
#endif
                   "ETH0", buf, sizeof(buf));
  fail_unless(res == IF2IP_NOT_FOUND,
              "case-mismatched interface name must not match");

  /* same case-sensitivity applies to the "wrong address family" branch */
  buf[0] = '\0';
  res = if2ip_scan(&wlan0, AF_INET,
#ifdef USE_IPV6
                   IPV6_SCOPE_GLOBAL, 0,
#endif
                   "wlan0", buf, sizeof(buf));
  fail_unless(res == IF2IP_AF_NOT_SUPPORTED,
              "exact match on wrong af should report AF_NOT_SUPPORTED");

  buf[0] = '\0';
  res = if2ip_scan(&wlan0, AF_INET,
#ifdef USE_IPV6
                   IPV6_SCOPE_GLOBAL, 0,
#endif
                   "WLAN0", buf, sizeof(buf));
  fail_unless(res == IF2IP_NOT_FOUND,
              "case-mismatched interface name must not match "
              "even on the wrong address family path");

  UNITTEST_END_SIMPLE
}

#else

static CURLcode test_unit1688(const char *arg)
{
  UNITTEST_BEGIN_SIMPLE
  UNITTEST_END_SIMPLE
}

#endif
