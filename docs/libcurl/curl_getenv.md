---
c: Copyright (C) Daniel Stenberg, <daniel@haxx.se>, et al.
SPDX-License-Identifier: curl
Title: curl_getenv
Section: 3
Source: libcurl
See-also:
  - curl_free (3)
  - curl_global_init_mem (3)
  - getenv (3C)
Protocol:
  - All
Added-in: 7.1
---

# NAME

curl_getenv - return value for environment name

# SYNOPSIS

~~~c
#include <curl/curl.h>

char *curl_getenv(const char *name);
~~~

# DESCRIPTION

curl_getenv() is a portable wrapper for the getenv() function, meant to
emulate its behavior and provide an identical interface for all operating
systems libcurl builds on (including Windows).

You must curl_free(3) the returned string when you are done with it and,
although not constrained by its type, it may not be altered.

curl_getenv(3) is usable before curl_global_init(3) (or
curl_global_init_mem(3)) is called. Once it has returned a non-NULL string,
a later curl_global_init_mem(3) call fails rather than replace the memory
functions used to allocate that string.

# %PROTOCOLS%

# EXAMPLE

~~~c
int main(void)
{
  char *width = curl_getenv("COLUMNS");
  if(width) {
    /* it was set */
    curl_free(width);
  }
}
~~~

# %AVAILABILITY%

# RETURN VALUE

A pointer to a null-terminated string or NULL if it failed to find the
specified name.

# NOTE

Under Unix operating systems, there is no point in returning an allocated
memory, although other systems does not work properly if this is not done. The
Unix implementation thus suffers slightly from the drawbacks of other systems.
