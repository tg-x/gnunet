/*
     This file is part of GNUnet.
     Copyright (C) 2001, 2002, 2003, 2004, 2006, 2012 Christian Grothoff (and other contributing authors)

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
 * @file util/common_endian.c
 * @brief endian conversion helpers
 * @author Christian Grothoff
 * @author Gabor X Toth
 */

#include "platform.h"
#include "gnunet_util_lib.h"

#define LOG(kind,...) GNUNET_log_from (kind, "util",__VA_ARGS__)


uint64_t
GNUNET_htonll (uint64_t n)
{
#if __BYTE_ORDER == __BIG_ENDIAN
  return n;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
  return (((uint64_t) htonl (n)) << 32) + htonl (n >> 32);
#else
  #error byteorder undefined
#endif
}


uint64_t
GNUNET_ntohll (uint64_t n)
{
#if __BYTE_ORDER == __BIG_ENDIAN
  return n;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
  return (((uint64_t) ntohl (n)) << 32) + ntohl (n >> 32);
#else
  #error byteorder undefined
#endif
}


/**
 * Convert double to network-byte-order.
 * @param d the value in network byte order
 * @return the same value in host byte order
 */
double
GNUNET_hton_double (double d)
{
  double res;
  uint64_t *in = (uint64_t *) &d;
  uint64_t *out = (uint64_t *) &res;

  out[0] = GNUNET_htonll(in[0]);

  return res;
}


/**
 * Convert double to host-byte-order
 * @param d the value in network byte order
 * @return the same value in host byte order
 */
double
GNUNET_ntoh_double (double d)
{
  double res;
  uint64_t *in = (uint64_t *) &d;
  uint64_t *out = (uint64_t *) &res;

  out[0] = GNUNET_ntohll(in[0]);

  return res;
}


uint64_t
GNUNET_htonll_signed (int64_t n)
{
  return GNUNET_htonll (n - INT64_MIN);
}


int64_t
GNUNET_ntohll_signed (uint64_t n)
{
  return GNUNET_ntohll (n) + INT64_MIN;
}


uint32_t
GNUNET_htonl_signed (int32_t n)
{
  return htonl (n - INT32_MIN);
}


int32_t
GNUNET_ntohl_signed (uint32_t n)
{
  return ntohl (n) + INT32_MIN;
}


uint16_t
GNUNET_htons_signed (int16_t n)
{
  return htons (n - INT16_MIN);
}


int16_t
GNUNET_ntohs_signed (uint16_t n)
{
  return ntohs (n) + INT16_MIN;
}



/* end of common_endian.c */
