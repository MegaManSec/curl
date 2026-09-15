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
/* Pause a TFTP upload mid-block and resume it, then verify the server
 * received the entire payload instead of a truncated one.
 */
#include "first.h"

#define TEST1727_PAUSE_TIME 2

struct t1727_ReadThis {
  const char *data;
  size_t size;
  size_t sent;
  time_t origin;
  int paused;
};

static size_t t1727_read_cb(char *ptr, size_t size, size_t nmemb,
                            void *userp)
{
  struct t1727_ReadThis *pooh = (struct t1727_ReadThis *)userp;
  size_t buffer_size = size * nmemb;
  size_t remaining = pooh->size - pooh->sent;

  if(!remaining)
    return 0;

  if(!pooh->sent) {
    size_t first = 5;
    if(first > buffer_size)
      first = buffer_size;
    memcpy(ptr, pooh->data, first);
    pooh->sent += first;
    return first;
  }

  if(!pooh->paused) {
    pooh->paused = 1;
    pooh->origin = time(NULL);
    return CURL_READFUNC_PAUSE;
  }

  if(remaining > buffer_size)
    remaining = buffer_size;
  memcpy(ptr, pooh->data + pooh->sent, remaining);
  pooh->sent += remaining;
  return remaining;
}

static CURLcode test_lib1727(const char *URL)
{
  static const char testdata[] = "0123456789ABCDEFGHIJ";
  struct t1727_ReadThis pooh;
  CURL *curl = NULL;
  CURLM *multi = NULL;
  CURLcode result = TEST_ERR_FAILURE;
  CURLMcode mresult = CURLM_OK;
  int mrunning = 0;

  pooh.data = testdata;
  pooh.size = sizeof(testdata) - 1;
  pooh.sent = 0;
  pooh.paused = 0;
  pooh.origin = 0;

  if(curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK)
    return TEST_ERR_MAJOR_BAD;

  curl = curl_easy_init();
  if(!curl) {
    curl_global_cleanup();
    return TEST_ERR_MAJOR_BAD;
  }

  easy_setopt(curl, CURLOPT_URL, URL);
  easy_setopt(curl, CURLOPT_UPLOAD, 1L);
  easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)pooh.size);
  easy_setopt(curl, CURLOPT_READFUNCTION, t1727_read_cb);
  easy_setopt(curl, CURLOPT_READDATA, &pooh);
  easy_setopt(curl, CURLOPT_TFTP_NO_OPTIONS, 1L);
  easy_setopt(curl, CURLOPT_VERBOSE, 1L);

  multi = curl_multi_init();
  if(!multi) {
    result = TEST_ERR_MAJOR_BAD;
    goto test_cleanup;
  }

  multi_add_handle(multi, curl);

  while(!mresult) {
    struct timeval timeout;
    fd_set fdread;
    fd_set fdwrite;
    fd_set fdexcept;
    int maxfd = -1;

    mresult = curl_multi_perform(multi, &mrunning);
    if(mresult || !mrunning)
      break;

    if(pooh.paused) {
      time_t delta = time(NULL) - pooh.origin;

      if(delta >= 4 * TEST1727_PAUSE_TIME) {
        curl_mfprintf(stderr, "unpausing failed: drain problem?\n");
        result = CURLE_OPERATION_TIMEDOUT;
        goto test_cleanup;
      }

      if(delta >= TEST1727_PAUSE_TIME)
        curl_easy_pause(curl, CURLPAUSE_CONT);
    }

    FD_ZERO(&fdread);
    FD_ZERO(&fdwrite);
    FD_ZERO(&fdexcept);
    timeout.tv_sec = 0;
    timeout.tv_usec = 100000;
    mresult = curl_multi_fdset(multi, &fdread, &fdwrite, &fdexcept, &maxfd);
    if(mresult)
      break;
#ifdef _WIN32
    if(maxfd == -1)
      curlx_wait_ms(100);
    else
#endif
    select(maxfd + 1, &fdread, &fdwrite, &fdexcept, &timeout);
  }

  if(mresult != CURLM_OK)
    result = TEST_ERR_MULTI;
  else {
    for(;;) {
      int msgs_left;
      CURLMsg *msg = curl_multi_info_read(multi, &msgs_left);
      if(!msg)
        break;
      if(msg->msg == CURLMSG_DONE)
        result = msg->data.result;
    }
  }

test_cleanup:
  if(multi) {
    curl_multi_remove_handle(multi, curl);
    curl_multi_cleanup(multi);
  }
  curl_easy_cleanup(curl);
  curl_global_cleanup();

  return result;
}
