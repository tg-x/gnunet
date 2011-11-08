/*
     This file is part of GNUnet.
     (C) 2009 Christian Grothoff (and other contributing authors)

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
 * @file hello/address.c
 * @brief helper functions for handling addresses
 * @author Christian Grothoff
 */
#include "platform.h"
#include "gnunet_hello_lib.h"
#include "gnunet_util_lib.h"


/**
 * Allocate an address struct.
 *
 * @param peer the peer
 * @param transport_name plugin name
 * @param address binary address
 * @param address_length number of bytes in 'address'
 * @return the address struct
 */
struct GNUNET_HELLO_Address *
GNUNET_HELLO_address_allocate (const struct GNUNET_PeerIdentity *peer,
			       const char *transport_name,
			       const void *address,
			       size_t address_length)
{
  struct GNUNET_HELLO_Address *addr;
  size_t slen;
  char *end;

  slen = strlen (transport_name) + 1;
  addr = GNUNET_malloc (sizeof (struct GNUNET_HELLO_Address) + 
			address_length + 
			slen);
  addr->peer = *peer;
  addr->address = &addr[1];
  end = (char*) &addr[1];
  memcpy (end, address, address_length);
  addr->address_length = address_length;
  addr->transport_name = &end[address_length];
  memcpy (&end[address_length], transport_name, slen);
  return addr;
}

/* end of address.c */
