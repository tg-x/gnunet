/*
     This file is part of GNUnet.
     (C) 2009, 2010 Christian Grothoff (and other contributing authors)

     GNUnet is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published
     by the Free Software Foundation; either version 3, or (at your
     option) any later version.

     GNUnet is distributed in the hope that it will be useful, but
     WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     General Public License for more details.

     You should have received a copy of the GNU General Public License
     along with GNUnet; see the file COPYING.  If not, write to the
     Free Software Foundation, Inc., 59 Temple Place - Suite 330,
     Boston, MA 02111-1307, USA.
*/
/**
 * @file transport/test_http_common.c
 * @brief base test case for common http functionality
 */
#include "platform.h"
#include "gnunet_transport_service.h"
#include "transport-testing.h"
#include "plugin_transport_http_common.h"

struct SplittedHTTPAddress
{
	char *protocol;
	char *host;
	char *path;
	int port;
};

void
clean (struct SplittedHTTPAddress *addr)
{
	if (NULL != addr)
	{
		GNUNET_free_non_null (addr->host);
		GNUNET_free_non_null (addr->path);
		GNUNET_free_non_null (addr->protocol);
		GNUNET_free_non_null (addr);
	}
}


int
check (struct SplittedHTTPAddress *addr,
			 char * protocol,
			 char * host,
			 int port,
			 char * path)
{

	if (NULL == addr)
		return GNUNET_NO;
	if (((NULL == addr->protocol) && (NULL != protocol)) ||
			((NULL != addr->protocol) && (NULL == protocol)))
	{
		GNUNET_break (0);
		return GNUNET_NO;
	}
	else if ((NULL != addr->protocol) && (NULL != protocol))
	{
		if (0 != strcmp(addr->protocol, protocol))
		{
			GNUNET_break (0);
			return GNUNET_NO;
		}
	}

	if (((NULL == addr->host) && (NULL != host)) ||
			((NULL != addr->host) && (NULL == host)))
	{
		GNUNET_break (0);
		return GNUNET_NO;
	}
	else if ((NULL != addr->host) && (NULL != host))
	{
		if (0 != strcmp(addr->host, host))
		{
			GNUNET_break (0);
			return GNUNET_NO;
		}
	}


	if (((NULL == addr->path) && (NULL != path)) ||
			((NULL != addr->path) && (NULL == path)))
	{
		GNUNET_break (0);
		return GNUNET_NO;
	}
	else if ((NULL != addr->path) && (NULL != path))
	{
		if (0 != strcmp(addr->path, path))
		{
			GNUNET_break (0);
			return GNUNET_NO;
		}
	}

	if ((addr->port != port))
	{
		GNUNET_break (0);
		return GNUNET_NO;
	}

	return GNUNET_OK;
}

void
test_hostname ()
{
  struct SplittedHTTPAddress * spa;
  spa = http_split_address ("http://test.local");
  if (NULL == spa)
  {
  	GNUNET_break (0);
  }
  else
  {
  		if (GNUNET_OK != check(spa, "http", "test.local", HTTP_DEFAULT_PORT, ""))
  		{
  				GNUNET_break (0);
  		}
  		clean (spa);
  }

  spa = http_split_address ("http://test.local");
  if (NULL == spa)
  {
  	GNUNET_break (0);
  }
  else
  {
  		if (GNUNET_OK != check(spa, "http", "test.local", HTTP_DEFAULT_PORT, ""))
  		{
  				GNUNET_break (0);
  		}
  		clean (spa);
  }


  spa = http_split_address ("http://test.local/");
  if (NULL == spa)
  {
  	GNUNET_break (0);
  }
  else
  {
  		if (GNUNET_OK != check(spa, "http", "test.local", HTTP_DEFAULT_PORT, "/"))
  		{
  				GNUNET_break (0);
  		}
  		clean (spa);
  }

  spa = http_split_address ("http://test.local/path");
  if (NULL == spa)
  {
  	GNUNET_break (0);
  }
  else
  {
  		if (GNUNET_OK != check(spa, "http", "test.local", HTTP_DEFAULT_PORT, "/path"))
  		{
  				GNUNET_break (0);
  		}
  		clean (spa);
  }

  spa = http_split_address ("http://test.local/path/");
  if (NULL == spa)
  {
  	GNUNET_break (0);
  }
  else
  {
  		if (GNUNET_OK != check(spa, "http", "test.local", HTTP_DEFAULT_PORT, "/path/"))
  		{
  				GNUNET_break (0);
  		}
  		clean (spa);


  }

  spa = http_split_address ("http://test.local:1000/path/");
  if (NULL == spa)
  {
  	GNUNET_break (0);
  }
  else
  {
  		if (GNUNET_OK != check(spa, "http", "test.local", 1000, "/path/"))
  		{
  				GNUNET_break (0);
  		}
  		clean (spa);
  }
}

void
test_ipv4 ()
{
  struct SplittedHTTPAddress * spa;
  spa = http_split_address ("http://127.0.0.1");
  if (NULL == spa)
  {
  	GNUNET_break (0);
  }
  else
  {
  		if (GNUNET_OK != check(spa, "http", "127.0.0.1", HTTP_DEFAULT_PORT, ""))
  		{
  				GNUNET_break (0);
  		}
  		clean (spa);
  }

  spa = http_split_address ("http://127.0.0.1");
  if (NULL == spa)
  {
  	GNUNET_break (0);
  }
  else
  {
  		if (GNUNET_OK != check(spa, "http", "127.0.0.1", HTTP_DEFAULT_PORT, ""))
  		{
  				GNUNET_break (0);
  		}
  		clean (spa);
  }


  spa = http_split_address ("http://127.0.0.1/");
  if (NULL == spa)
  {
  	GNUNET_break (0);
  }
  else
  {
  		if (GNUNET_OK != check(spa, "http", "127.0.0.1", HTTP_DEFAULT_PORT, "/"))
  		{
  				GNUNET_break (0);
  		}
  		clean (spa);
  }

  spa = http_split_address ("http://127.0.0.1/path");
  if (NULL == spa)
  {
  	GNUNET_break (0);
  }
  else
  {
  		if (GNUNET_OK != check(spa, "http", "127.0.0.1", HTTP_DEFAULT_PORT, "/path"))
  		{
  				GNUNET_break (0);
  		}
  		clean (spa);
  }

  spa = http_split_address ("http://127.0.0.1/path/");
  if (NULL == spa)
  {
  	GNUNET_break (0);
  }
  else
  {
  		if (GNUNET_OK != check(spa, "http", "127.0.0.1", HTTP_DEFAULT_PORT, "/path/"))
  		{
  				GNUNET_break (0);
  		}
  		clean (spa);


  }

  spa = http_split_address ("http://127.0.0.1:1000/path/");
  if (NULL == spa)
  {
  	GNUNET_break (0);
  }
  else
  {
  		if (GNUNET_OK != check(spa, "http", "127.0.0.1", 1000, "/path/"))
  		{
  				GNUNET_break (0);
  		}
  		clean (spa);
  }
}

void
test_ipv6 ()
{
  struct SplittedHTTPAddress * spa;
  spa = http_split_address ("http://[::1]");
  if (NULL == spa)
  {
  	GNUNET_break (0);
  }
  else
  {
  		if (GNUNET_OK != check(spa, "http", "[::1]", HTTP_DEFAULT_PORT, ""))
  		{
  				GNUNET_break (0);
  		}
  		clean (spa);
  }

  spa = http_split_address ("http://[::1]");
  if (NULL == spa)
  {
  	GNUNET_break (0);
  }
  else
  {
  		if (GNUNET_OK != check(spa, "http", "[::1]", HTTP_DEFAULT_PORT, ""))
  		{
  				GNUNET_break (0);
  		}
  		clean (spa);
  }


  spa = http_split_address ("http://[::1]/");
  if (NULL == spa)
  {
  	GNUNET_break (0);
  }
  else
  {
  		if (GNUNET_OK != check(spa, "http", "[::1]", HTTP_DEFAULT_PORT, "/"))
  		{
  				GNUNET_break (0);
  		}
  		clean (spa);
  }

  spa = http_split_address ("http://[::1]/path");
  if (NULL == spa)
  {
  	GNUNET_break (0);
  }
  else
  {
  		if (GNUNET_OK != check(spa, "http", "[::1]", HTTP_DEFAULT_PORT, "/path"))
  		{
  				GNUNET_break (0);
  		}
  		clean (spa);
  }

  spa = http_split_address ("http://[::1]/path/");
  if (NULL == spa)
  {
  	GNUNET_break (0);
  }
  else
  {
  		if (GNUNET_OK != check(spa, "http", "[::1]", HTTP_DEFAULT_PORT, "/path/"))
  		{
  				GNUNET_break (0);
  		}
  		clean (spa);


  }

  spa = http_split_address ("http://[::1]:1000/path/");
  if (NULL == spa)
  {
  	GNUNET_break (0);
  }
  else
  {
  		if (GNUNET_OK != check(spa, "http", "[::1]", 1000, "/path/"))
  		{
  				GNUNET_break (0);
  		}
  		clean (spa);
  }
}

int
main (int argc, char *argv[])
{
  int ret = 0;
  struct SplittedHTTPAddress * spa;
  GNUNET_log_setup ("test", "DEBUG", NULL);

  spa = http_split_address ("");
  if (NULL != spa)
  {
  	clean (spa);
  	GNUNET_break (0);
  }

  http_split_address ("http://");
  if (NULL != spa)
  {
		clean (spa);
  	GNUNET_break (0);
  }

  http_split_address ("://");
  if (NULL != spa)
  {
		clean (spa);
  	GNUNET_break (0);
  }

  test_hostname ();
  test_ipv4 ();
  test_ipv6 ();

  return ret;
}

/* end of test_http_common.c */
