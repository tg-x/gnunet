/*
     This file is part of GNUnet.
     (C) 2001-2013 Christian Grothoff (and other contributing authors)

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
 * @file mesh/gnunet-service-mesh-enc.c
 * @brief GNUnet MESH service with encryption
 * @author Bartlomiej Polot
 *
 *  FIXME in progress:
 * - when sending in-order buffered data, wait for client ACKs
 * - add signatures
 * - add encryption
 * - set connection IDs independently from tunnel, tunnel has no ID
 *
 * TODO:
 * - relay corking down to core
 * - set ttl relative to path length
 * TODO END
 *
 * Dictionary:
 * - peer: other mesh instance. If there is direct connection it's a neighbor.
 * - tunnel: encrypted connection to a peer, neighbor or not.
 * - channel: connection between two clients, on the same or different peers.
 *            have properties like reliability.
 * - path: series of directly connected peer from one peer to another.
 * - connection: path which is being used in a tunnel.
 */

#include "platform.h"
#include "gnunet_util_lib.h"
#include "mesh_enc.h"
#include "gnunet_statistics_service.h"

#include "gnunet-service-mesh_local.h"
#include "gnunet-service-mesh_channel.h"
#include "gnunet-service-mesh_connection.h"
#include "gnunet-service-mesh_tunnel.h"
#include "gnunet-service-mesh_dht.h"
#include "gnunet-service-mesh_peer.h"


/******************************************************************************/
/************************      DATA STRUCTURES     ****************************/
/******************************************************************************/



/******************************************************************************/
/************************      DEBUG FUNCTIONS     ****************************/
/******************************************************************************/


/******************************************************************************/
/***********************      GLOBAL VARIABLES     ****************************/
/******************************************************************************/

/****************************** Global variables ******************************/

/**
 * Handle to the statistics service.
 */
struct GNUNET_STATISTICS_Handle *stats;

/**
 * Local peer own ID (memory efficient handle).
 */
GNUNET_PEER_Id myid;

/**
 * Local peer own ID (full value).
 */
struct GNUNET_PeerIdentity my_full_id;

/*************************** Static global variables **************************/

/**
 * Own private key.
 */
static struct GNUNET_CRYPTO_EddsaPrivateKey *my_private_key;


/******************************************************************************/
/***********************         DECLARATIONS        **************************/
/******************************************************************************/


/******************************************************************************/
/******************      GENERAL HELPER FUNCTIONS      ************************/
/******************************************************************************/



/******************************************************************************/
/****************      MESH NETWORK HANDLER HELPERS     ***********************/
/******************************************************************************/



/******************************************************************************/
/********************      MESH NETWORK HANDLERS     **************************/
/******************************************************************************/


/******************************************************************************/
/************************      MAIN FUNCTIONS      ****************************/
/******************************************************************************/


/**
 * Task run during shutdown.
 *
 * @param cls unused
 * @param tc unused
 */
static void
shutdown_task (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "shutting down\n");

  GML_shutdown ();
  GMD_shutdown ();
  GMP_shutdown ();
  GMC_shutdown ();
  GMT_shutdown ();

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "shut down\n");
}


/**
 * Process mesh requests.
 *
 * @param cls closure
 * @param server the initialized server
 * @param c configuration to use
 */
static void
run (void *cls, struct GNUNET_SERVER_Handle *server,
     const struct GNUNET_CONFIGURATION_Handle *c)
{
  struct GNUNET_CRYPTO_EddsaPrivateKey *pk;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "starting to run\n");

  stats = GNUNET_STATISTICS_create ("mesh", c);

  /* Scheduled the task to clean up when shutdown is called */
  GNUNET_SCHEDULER_add_delayed (GNUNET_TIME_UNIT_FOREVER_REL, &shutdown_task,
                                NULL);
  GNUNET_log (GNUNET_ERROR_TYPE_INFO, "reading key\n");
  pk = GNUNET_CRYPTO_eddsa_key_create_from_configuration (c);
  GNUNET_assert (NULL != pk);
  my_private_key = pk;
  GNUNET_CRYPTO_eddsa_key_get_public (my_private_key, &my_full_id.public_key);
  myid = GNUNET_PEER_intern (&my_full_id);
  GNUNET_log (GNUNET_ERROR_TYPE_INFO,
              "Mesh for peer [%s] starting\n",
              GNUNET_i2s (&my_full_id));

  GML_init (server);    /* Local clients */
  GMC_init (c);         /* Connections */
  GMP_init (c);         /* Peers */
  GMD_init (c);         /* DHT */
  GMT_init (c, my_private_key); /* Tunnels */

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Mesh service running\n");
}


/**
 * The main function for the mesh service.
 *
 * @param argc number of arguments from the command line
 * @param argv command line arguments
 * @return 0 ok, 1 on error
 */
int
main (int argc, char *const *argv)
{
  int ret;
  int r;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "main()\n");
  r = GNUNET_SERVICE_run (argc, argv, "mesh", GNUNET_SERVICE_OPTION_NONE, &run,
                          NULL);
  ret = (GNUNET_OK == r) ? 0 : 1;
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "main() END\n");

  return ret;
}
