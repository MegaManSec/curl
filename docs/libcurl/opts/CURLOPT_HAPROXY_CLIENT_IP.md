---
c: Copyright (C) Daniel Stenberg, <daniel@haxx.se>, et al.
SPDX-License-Identifier: curl
Title: CURLOPT_HAPROXY_CLIENT_IP
Section: 3
Source: libcurl
Protocol:
  - All
See-also:
  - CURLOPT_HAPROXYPROTOCOL (3)
  - CURLOPT_PROXY (3)
Added-in: 8.2.0
---

# NAME

CURLOPT_HAPROXY_CLIENT_IP - set HAProxy PROXY protocol client IP

# SYNOPSIS

~~~c
#include <curl/curl.h>

CURLcode curl_easy_setopt(CURL *handle, CURLOPT_HAPROXY_CLIENT_IP,
                          char *client_ip);
~~~

# DESCRIPTION

When this parameter is set to a valid IPv4 or IPv6 numerical address in its
printable ASCII string version, the library sends this as the client address
in the HAProxy PROXY protocol v1 header at beginning of the connection.

The client address is reported upstream as the source *and* destination address
of the non-existing client connection (since 8.20.0).

This option is an alternative to CURLOPT_HAPROXYPROTOCOL(3) as that one cannot
use a specified address.

Using this option multiple times makes the last set string override the
previous ones. Set it to NULL to disable its use again.

The application does not have to keep the string around after setting this
option.

libcurl rejects strings that are not a syntactically valid IPv4 or IPv6
numerical address, so the resulting PROXY protocol header line cannot be
manipulated by passing in extra characters such as CR or LF.

A connection is only reused for a subsequent request if that request uses
the same client IP (or lack thereof); otherwise libcurl transparently opens
a new connection instead of resending this header on a connection
established with a different client IP.

# DEFAULT

NULL, no HAProxy header is sent

# %PROTOCOLS%

# EXAMPLE

~~~c
int main(void)
{
  CURL *curl = curl_easy_init();
  if(curl) {
    CURLcode result;
    curl_easy_setopt(curl, CURLOPT_URL, "https://example.com/");
    curl_easy_setopt(curl, CURLOPT_HAPROXY_CLIENT_IP, "1.1.1.1");
    result = curl_easy_perform(curl);
  }
}
~~~

# %AVAILABILITY%

# RETURN VALUE

curl_easy_setopt(3) returns a CURLcode indicating success or error.

CURLE_OK (0) means everything was OK, non-zero means an error occurred, see
libcurl-errors(3).

Returns CURLE_BAD_FUNCTION_ARGUMENT if the string is not a valid IPv4 or
IPv6 address.
