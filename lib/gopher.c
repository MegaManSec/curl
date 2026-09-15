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
#include "urldata.h"
#include "gopher.h"

#ifndef CURL_DISABLE_GOPHER

#include "transfer.h"
#include "sendf.h"
#include "curl_trc.h"
#include "cfilters.h"
#include "connect.h"
#include "url.h"
#include "escape.h"

#define CURL_META_GOPHER_EASY "meta:proto:gopher:easy"

/* The pending gopher request (selector followed by CRLF) and how much of
   it has been sent to the server so far. */
struct gopher_state {
  char *req;      /* selector followed by CRLF */
  size_t req_len; /* total length of req */
  size_t sel_len; /* length of the selector part of req */
  size_t sent;    /* number of bytes of req sent so far */
};

static void gopher_easy_dtor(const void *key, size_t klen, void *entry)
{
  struct gopher_state *gs = entry;
  (void)key;
  (void)klen;
  curlx_free(gs->req);
  curlx_free(gs);
}

#ifdef USE_SSL
static CURLcode gopher_connect(struct Curl_easy *data, bool *done)
{
  (void)data;
  (void)done;
  return CURLE_OK;
}

static CURLcode gopher_connecting(struct Curl_easy *data, bool *done)
{
  struct connectdata *conn = data->conn;
  CURLcode result;

  result = Curl_conn_connect(data, FIRSTSOCKET, TRUE, done);
  if(result)
    connclose(conn);
  *done = TRUE;
  return result;
}
#endif

/* Make one non-blocking attempt at sending the pending gopher request. */
static CURLcode gopher_send(struct Curl_easy *data, bool *done)
{
  struct gopher_state *gs = Curl_meta_get(data, CURL_META_GOPHER_EASY);
  CURLcode result;
  size_t nwritten = 0;
  size_t len;

  DEBUGASSERT(gs);
  *done = FALSE;

  if(gs->sent < gs->req_len) {
    len = (gs->sent < gs->sel_len) ?
      (gs->sel_len - gs->sent) : (gs->req_len - gs->sent);

    result = Curl_xfer_send(data, gs->req + gs->sent, len, FALSE, &nwritten);
    if(result) {
      failf(data, "Failed sending Gopher request");
      return result;
    }

    if(gs->sent < gs->sel_len && nwritten) {
      result = Curl_client_write(data, CLIENTWRITE_HEADER,
                                 gs->req + gs->sent, nwritten);
      if(result)
        return result;
    }
    gs->sent += nwritten;

    if(gs->sent < gs->req_len)
      return CURLE_OK; /* wait for the socket to become writable again */
  }

  /* defer writing the CRLF to the client to preserve the historical
     behavior of this file */
  result = Curl_client_write(data, CLIENTWRITE_HEADER, "\r\n", 2);
  if(result)
    return result;

  Curl_xfer_setup_recv(data, FIRSTSOCKET, -1);
  *done = TRUE;
  return CURLE_OK;
}

static CURLcode gopher_doing(struct Curl_easy *data, bool *done)
{
  return gopher_send(data, done);
}

static CURLcode gopher_do(struct Curl_easy *data, bool *done)
{
  CURLcode result = CURLE_OK;
  char *gopherpath;
  const char *path = data->state.up.path;
  const char *query = data->state.up.query;
  char *sel = NULL;
  size_t sel_len = 0;
  struct gopher_state *gs;

  *done = FALSE;

  /* path is guaranteed non-NULL */
  DEBUGASSERT(path);

  if(query)
    gopherpath = curl_maprintf("%s?%s", path, query);
  else
    gopherpath = curlx_strdup(path);

  if(!gopherpath)
    return CURLE_OUT_OF_MEMORY;

  /* Create selector. Degenerate cases: / and /1 => convert to "" */
  if(strlen(gopherpath) <= 2) {
    curlx_free(gopherpath);
  }
  else {
    const char *newp;

    /* Otherwise, drop / and the first character (i.e., item type) ... */
    newp = gopherpath;
    newp += 2;

    /* ... and finally unescape */
    result = Curl_urldecode(newp, 0, &sel, &sel_len, REJECT_ZERO);
    curlx_free(gopherpath);
    if(result)
      return result;

    /* A decoded CR or LF would terminate the single-line gopher request and
       let a crafted URL smuggle additional bytes onto the wire. REJECT_ZERO
       only blocks NUL; reject CR and LF here too. A TAB is left alone as it
       is the legitimate gopher type-7 selector/search separator. */
    if(memchr(sel, '\r', sel_len) || memchr(sel, '\n', sel_len)) {
      curlx_free(sel);
      failf(data, "Bad gopher selector, CR or LF not allowed");
      return CURLE_URL_MALFORMAT;
    }
  }

  gs = curlx_calloc(1, sizeof(*gs));
  if(!gs) {
    curlx_free(sel);
    return CURLE_OUT_OF_MEMORY;
  }

  gs->req = curlx_malloc(sel_len + 2);
  if(!gs->req) {
    curlx_free(sel);
    curlx_free(gs);
    return CURLE_OUT_OF_MEMORY;
  }
  if(sel_len)
    memcpy(gs->req, sel, sel_len);
  curlx_free(sel);
  memcpy(gs->req + sel_len, "\r\n", 2);
  gs->sel_len = sel_len;
  gs->req_len = sel_len + 2;

  if(Curl_meta_set(data, CURL_META_GOPHER_EASY, gs, gopher_easy_dtor))
    return CURLE_OUT_OF_MEMORY;

  return gopher_send(data, done);
}

/*
 * Gopher protocol handler.
 * This is also a nice simple template to build off for simple
 * connect-command-download protocols.
 */

const struct Curl_protocol Curl_protocol_gopher = {
  ZERO_NULL,                            /* setup_connection */
  gopher_do,                            /* do_it */
  ZERO_NULL,                            /* done */
  ZERO_NULL,                            /* do_more */
  ZERO_NULL,                            /* connect_it */
  ZERO_NULL,                            /* connecting */
  gopher_doing,                         /* doing */
  ZERO_NULL,                            /* proto_pollset */
  ZERO_NULL,                            /* doing_pollset */
  ZERO_NULL,                            /* domore_pollset */
  ZERO_NULL,                            /* perform_pollset */
  ZERO_NULL,                            /* disconnect */
  ZERO_NULL,                            /* write_resp */
  ZERO_NULL,                            /* write_resp_hd */
  ZERO_NULL,                            /* connection_is_dead */
  ZERO_NULL,                            /* attach connection */
  ZERO_NULL,                            /* follow */
};

#ifdef USE_SSL
const struct Curl_protocol Curl_protocol_gophers = {
  ZERO_NULL,                            /* setup_connection */
  gopher_do,                            /* do_it */
  ZERO_NULL,                            /* done */
  ZERO_NULL,                            /* do_more */
  gopher_connect,                       /* connect_it */
  gopher_connecting,                    /* connecting */
  gopher_doing,                         /* doing */
  ZERO_NULL,                            /* proto_pollset */
  ZERO_NULL,                            /* doing_pollset */
  ZERO_NULL,                            /* domore_pollset */
  ZERO_NULL,                            /* perform_pollset */
  ZERO_NULL,                            /* disconnect */
  ZERO_NULL,                            /* write_resp */
  ZERO_NULL,                            /* write_resp_hd */
  ZERO_NULL,                            /* connection_is_dead */
  ZERO_NULL,                            /* attach connection */
  ZERO_NULL,                            /* follow */
};
#endif

#endif /* CURL_DISABLE_GOPHER */
