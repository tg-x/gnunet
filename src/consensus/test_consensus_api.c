/*
     This file is part of GNUnet.
     (C) 2012 Christian Grothoff (and other contributing authors)

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
 * @file consensus/test_consensus_api.c
 * @brief testcase for consensus_api.c
 */
#include "platform.h"
#include "gnunet_util_lib.h"
#include "gnunet_consensus_service.h"
#include "gnunet_testing_lib-new.h"


static struct GNUNET_CONSENSUS_Handle *consensus;

static struct GNUNET_HashCode session_id;


void
on_new_element (void *cls,
                struct GNUNET_CONSENSUS_Element *element)
{
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "received new element\n");
}


static void
run (void *cls, 
     const struct GNUNET_CONFIGURATION_Handle *cfg,
     struct GNUNET_TESTING_Peer *peer)
{
  char *str = "foo";

  GNUNET_CRYPTO_hash (str, strlen (str), &session_id);
  consensus = GNUNET_CONSENSUS_create (cfg, 0, NULL, &session_id, on_new_element, cls);
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Connecting to consensus service.\n");
  GNUNET_assert (consensus != NULL);
}


int
main (int argc, char **argv)
{
  int ret;

  GNUNET_log_setup ("test_consensus_api",
                    "DEBUG",
                    NULL);

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "testing consensus api\n");

  ret = GNUNET_TESTING_peer_run ("test_consensus_api",
                                 "test_consensus.conf",
                                 &run, NULL);
  return ret;
}

