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
#include "strcase.h"
#include "curl_addrinfo.h"
#include "peer.h"
#include "protocol.h"
#include "vdns/dnscache.h"
#include "vdns/hostip.h"

static CURLcode t1732_setup(void)
{
  CURLcode result = CURLE_OK;
  global_init(CURL_GLOBAL_ALL);
  return result;
}

struct t1732_key {
  uint8_t data[255 + 3];
  size_t len;
};

static void t1732_create_key(struct t1732_key *key,
                             const char *hostname,
                             uint16_t port,
                             uint8_t type)
{
  size_t namelen = strlen(hostname);
  if(namelen > (sizeof(key->data) - 3))
    namelen = sizeof(key->data) - 3;
  /* store and lower case the name */
  key->data[0] = type;
  key->data[1] = (uint8_t)((port >> 8) & 0xff);
  key->data[2] = (uint8_t)(port & 0xff);
  Curl_strntolower((char *)key->data + 3, hostname, namelen);
  key->len = namelen + 3;
}

#ifdef USE_ARES
/*
 * Two easy handles sharing a DNS cache via the same multi handle, each
 * configured with a different CURLOPT_DNS_SERVERS, must not see each
 * other's cached answers; a handle with a matching value still should.
 */
static void t1732_dns_servers_rid(void)
{
  struct Curl_multi *multi = curl_multi_init();
  struct Curl_easy *e1 = curl_easy_init();
  struct Curl_easy *e2 = curl_easy_init();
  struct Curl_easy *e3 = curl_easy_init();
  struct Curl_peer *peer = NULL;
  struct Curl_addrinfo *addr = NULL;
  struct Curl_dns_entry *dns = NULL;

  if(!multi || !e1 || !e2 || !e3)
    goto out;

  curl_multi_add_handle(multi, e1);
  curl_multi_add_handle(multi, e2);
  curl_multi_add_handle(multi, e3);

  curl_easy_setopt(e1, CURLOPT_DNS_SERVERS, "127.0.0.1:1111");
  curl_easy_setopt(e2, CURLOPT_DNS_SERVERS, "127.0.0.1:2222");
  curl_easy_setopt(e3, CURLOPT_DNS_SERVERS, "127.0.0.1:1111");

  if(Curl_peer_create(e1, &Curl_scheme_http, "rid.example", 80, &peer) ||
     Curl_str2addr("127.0.0.1", 80, &addr))
    goto out;
  dns = Curl_dnsc_mk_addr(e1, CURL_DNSQ_A, &addr, peer);
  fail_unless(dns, "Curl_dnsc_mk_addr failed");
  if(dns)
    fail_if(Curl_dnscache_add(e1, dns), "Curl_dnscache_add failed");
  Curl_dns_entry_unlink(e1, &dns);
  Curl_peer_unlink(&peer);

  if(Curl_peer_create(e2, &Curl_scheme_http, "rid.example", 80, &peer))
    goto out;
  Curl_dnscache_get(e2, CURL_DNSQ_A, peer, &dns);
  fail_unless(!dns, "a differing CURLOPT_DNS_SERVERS must not cache-hit");
  Curl_dns_entry_unlink(e2, &dns);
  Curl_peer_unlink(&peer);

  if(Curl_peer_create(e3, &Curl_scheme_http, "rid.example", 80, &peer))
    goto out;
  Curl_dnscache_get(e3, CURL_DNSQ_A, peer, &dns);
  fail_unless(dns, "a matching CURLOPT_DNS_SERVERS should cache-hit");
  Curl_dns_entry_unlink(e3, &dns);
  Curl_peer_unlink(&peer);

out:
  Curl_peer_unlink(&peer);
  Curl_freeaddrinfo(addr);
  curl_easy_cleanup(e1);
  curl_easy_cleanup(e2);
  curl_easy_cleanup(e3);
  curl_multi_cleanup(multi);
}
#endif

static CURLcode test_unit1732(const char *arg)
{
  /* In builds without IPv6 support CURLOPT_RESOLVE should skip over those
     addresses, so we have to do that as well. */
  static const char skip = 0;
#ifdef USE_IPV6
#define IPV6ONLY(x) x
#else
#define IPV6ONLY(x) &skip
#endif

  UNITTEST_BEGIN(t1732_setup())

  struct testcase {
    /* host:port:address[,address]... */
    const char *optval;

    /* lowercase host and port to retrieve the addresses from hostcache */
    const char *host;
    uint16_t port;

    /* whether we expect a permanent or non-permanent cache entry */
    bool permanent;

    /* 0 to 9 addresses expected from hostcache */
    const char *address[10];
  };

  /* CURLOPT_RESOLVE address parsing tests */
  static const struct testcase tests[] = {
    /* spaces are not allowed, for now */
    { "test.com:80:127.0.0.1, 127.0.0.2",
      "test.com", 80, TRUE, { NULL, }
    },
    { "TEST.com:80:,,127.0.0.1,,,127.0.0.2,,,,::1,,,",
      "test.com", 80, TRUE, { "127.0.0.1", "127.0.0.2", IPV6ONLY("::1"), }
    },
    { "test.com:80:::1,127.0.0.1",
      "test.com", 80, TRUE, { IPV6ONLY("::1"), "127.0.0.1", }
    },
    { "test.com:80:[::1],127.0.0.1",
      "test.com", 80, TRUE, { IPV6ONLY("::1"), "127.0.0.1", }
    },
    { "test.com:80:::1",
      "test.com", 80, TRUE, { IPV6ONLY("::1"), }
    },
    { "test.com:80:[::1]",
      "test.com", 80, TRUE, { IPV6ONLY("::1"), }
    },
    { "test.com:80:127.0.0.1",
      "test.com", 80, TRUE, { "127.0.0.1", }
    },
    { "test.com:80:,127.0.0.1",
      "test.com", 80, TRUE, { "127.0.0.1", }
    },
    { "test.com:80:127.0.0.1,",
      "test.com", 80, TRUE, { "127.0.0.1", }
    },
    { "test.com:0:127.0.0.1",
      "test.com", 0, TRUE, { "127.0.0.1", }
    },
    { "+test.com:80:127.0.0.1,",
      "test.com", 80, FALSE, { "127.0.0.1", }
    },
  };

  size_t i;
  struct Curl_multi *multi = NULL;
  struct Curl_easy *easy = NULL;
  struct curl_slist *list = NULL;

  for(i = 0; i < CURL_ARRAYSIZE(tests); ++i) {
    size_t j;
    size_t addressnum = CURL_ARRAYSIZE(tests[i].address);
    struct Curl_addrinfo *addr;
    struct Curl_dns_entry *dns;
    struct t1732_key entry_id;
    bool problem = FALSE;
    easy = curl_easy_init();
    if(!easy)
      goto error;
    curl_easy_setopt(easy, CURLOPT_VERBOSE, 1L);

    /* create a multi handle and add the easy handle to it so that the
       hostcache is setup */
    multi = curl_multi_init();
    curl_multi_add_handle(multi, easy);

    list = curl_slist_append(NULL, tests[i].optval);
    if(!list)
      goto error;
    curl_easy_setopt(easy, CURLOPT_RESOLVE, list);

    Curl_loadhostpairs(easy);

    t1732_create_key(&entry_id, tests[i].host, tests[i].port, CURL_DNST_ADDR);
    dns = Curl_hash_pick(&multi->dnscache.entries,
                         entry_id.data, entry_id.len);

    addr = dns ? dns->addr : NULL;

    for(j = 0; j < addressnum; ++j) {
      uint16_t port = 0;
      char ipaddress[MAX_IPADR_LEN] = { 0 };

      if(!addr && !tests[i].address[j])
        break;

      if(tests[i].address[j] == &skip)
        continue;

      if(addr && sockaddr2string(addr->ai_addr, addr->ai_addrlen,
                                 ipaddress, &port)) {
        curl_mfprintf(stderr, "%s:%d tests[%zu] failed. "
                      "getaddressinfo failed.\n",
                      __FILE__, __LINE__, i);
        problem = TRUE;
        break;
      }

      if(addr && !tests[i].address[j]) {
        curl_mfprintf(stderr, "%s:%d tests[%zu] failed. the retrieved addr "
                      "is %s but tests[%zu].address[%zu] is NULL.\n",
                      __FILE__, __LINE__, i, ipaddress, i, j);
        problem = TRUE;
        break;
      }

      if(!addr && tests[i].address[j]) {
        curl_mfprintf(stderr, "%s:%d tests[%zu] failed. the retrieved addr "
                      "is NULL but tests[%zu].address[%zu] is %s.\n",
                      __FILE__, __LINE__, i, i, j, tests[i].address[j]);
        problem = TRUE;
        break;
      }

      if(!curl_strequal(ipaddress, tests[i].address[j])) {
        curl_mfprintf(stderr, "%s:%d tests[%zu] failed. the retrieved addr "
                      "%s is not equal to tests[%zu].address[%zu] %s.\n",
                      __FILE__, __LINE__, i, ipaddress, i, j,
                      tests[i].address[j]);
        problem = TRUE;
        break;
      }

      if(port != tests[i].port) {
        curl_mfprintf(stderr, "%s:%d tests[%zu] failed. the retrieved port "
                      "for tests[%zu].address[%zu] is %d "
                      "but tests[%zu].port is %d.\n",
                      __FILE__, __LINE__, i, i, j, port, i, tests[i].port);
        problem = TRUE;
        break;
      }

      if(!dns->permanent && tests[i].permanent) {
        curl_mfprintf(stderr,
                      "%s:%d tests[%zu] failed. the permanent bit is not set "
                      "but tests[%zu].permanent is TRUE\n",
                      __FILE__, __LINE__, i, i);
        problem = TRUE;
        break;
      }

      if(dns->permanent && !tests[i].permanent) {
        curl_mfprintf(stderr, "%s:%d tests[%zu] failed. the permanent bit "
                      "is set but tests[%zu].permanent is FALSE\n",
                      __FILE__, __LINE__, i, i);
        problem = TRUE;
        break;
      }

      if(!addr)
        break;

      addr = addr->ai_next;
    }

    curl_easy_cleanup(easy);
    easy = NULL;
    curl_multi_cleanup(multi);
    multi = NULL;
    curl_slist_free_all(list);
    list = NULL;

    if(problem) {
      unitfail++;
      continue;
    }
  }

#ifdef USE_ARES
  t1732_dns_servers_rid();
#endif

error:
  curl_easy_cleanup(easy);
  curl_multi_cleanup(multi);
  curl_slist_free_all(list);

  UNITTEST_END(curl_global_cleanup())
}
