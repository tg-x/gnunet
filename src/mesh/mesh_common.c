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
 * @file mesh/mesh_common.c
 * @brief MESH helper functions
 * @author Bartlomiej Polot
 */

#include "mesh.h"

/**
 * @brief Translate a fwd variable into a string representation, for logging.
 *
 * @param fwd Is FWD? (#GNUNET_YES or #GNUNET_NO)
 *
 * @return String representing FWD or BCK.
 */
char *
GM_f2s (int fwd)
{
  if (GNUNET_YES == fwd)
  {
    return "FWD";
  }
  else if (GNUNET_NO == fwd)
  {
    return "BCK";
  }
  else
  {
    GNUNET_break (0);
    return "";
  }
}

int
GM_is_pid_bigger (uint32_t bigger, uint32_t smaller)
{
    return (GNUNET_YES == PID_OVERFLOW (smaller, bigger) ||
            (bigger > smaller && GNUNET_NO == PID_OVERFLOW (bigger, smaller)));
}


uint32_t
GM_max_pid (uint32_t a, uint32_t b)
{
  if (GM_is_pid_bigger(a, b))
    return a;
  return b;
}


uint32_t
GM_min_pid (uint32_t a, uint32_t b)
{
  if (GM_is_pid_bigger(a, b))
    return b;
  return a;
}


#if !defined(GNUNET_CULL_LOGGING)
const char *
GM_m2s (uint16_t m)
{
  static char buf[32];
  const char *t;

  switch (m)
  {
      /**
       * Request the creation of a path
       */
    case GNUNET_MESSAGE_TYPE_MESH_CONNECTION_CREATE:
      t = "CONNECTION_CREATE";
      break;

      /**
       * Request the modification of an existing path
       */
    case GNUNET_MESSAGE_TYPE_MESH_CONNECTION_ACK:
      t = "CONNECTION_ACK";
      break;

      /**
       * Notify that a connection of a path is no longer valid
       */
    case GNUNET_MESSAGE_TYPE_MESH_CONNECTION_BROKEN:
      t = "CONNECTION_BROKEN";
      break;

      /**
       * At some point, the route will spontaneously change
       */
    case GNUNET_MESSAGE_TYPE_MESH_PATH_CHANGED:
      t = "PATH_CHANGED";
      break;

      /**
       * Transport payload data.
       */
    case GNUNET_MESSAGE_TYPE_MESH_DATA:
      t = "DATA";
      break;

    /**
     * Confirm receipt of payload data.
     */
    case GNUNET_MESSAGE_TYPE_MESH_DATA_ACK:
      t = "DATA_ACK";
      break;

      /**
       * Key exchange encapsulation.
       */
    case GNUNET_MESSAGE_TYPE_MESH_KX:
      t = "KX";
      break;

      /**
       * New ephemeral key.
       */
    case GNUNET_MESSAGE_TYPE_MESH_KX_EPHEMERAL:
      t = "KX_EPHEMERAL";
      break;

      /**
       * Challenge to test peer's session key.
       */
    case GNUNET_MESSAGE_TYPE_MESH_KX_PING:
      t = "KX_PING";
      break;

      /**
       * Answer to session key challenge.
       */
    case GNUNET_MESSAGE_TYPE_MESH_KX_PONG:
      t = "KX_PONG";
      break;

      /**
       * Request the destuction of a path
       */
    case GNUNET_MESSAGE_TYPE_MESH_CONNECTION_DESTROY:
      t = "CONNECTION_DESTROY";
      break;

      /**
       * ACK for a data packet.
       */
    case GNUNET_MESSAGE_TYPE_MESH_ACK:
      t = "ACK";
      break;

      /**
       * POLL for ACK.
       */
    case GNUNET_MESSAGE_TYPE_MESH_POLL:
      t = "POLL";
      break;

      /**
       * Announce origin is still alive.
       */
    case GNUNET_MESSAGE_TYPE_MESH_KEEPALIVE:
      t = "KEEPALIVE";
      break;

    /**
       * Connect to the mesh service, specifying subscriptions
       */
    case GNUNET_MESSAGE_TYPE_MESH_LOCAL_CONNECT:
      t = "LOCAL_CONNECT";
      break;

      /**
       * Ask the mesh service to create a new tunnel
       */
    case GNUNET_MESSAGE_TYPE_MESH_CHANNEL_CREATE:
      t = "CHANNEL_CREATE";
      break;

      /**
       * Ask the mesh service to destroy a tunnel
       */
    case GNUNET_MESSAGE_TYPE_MESH_CHANNEL_DESTROY:
      t = "CHANNEL_DESTROY";
      break;

      /**
       * Confirm the creation of a channel.
       */
    case GNUNET_MESSAGE_TYPE_MESH_CHANNEL_ACK:
      t = "CHANNEL_ACK";
      break;

      /**
       * Confirm the creation of a channel.
       */
    case GNUNET_MESSAGE_TYPE_MESH_CHANNEL_NACK:
      t = "CHANNEL_NACK";
      break;

      /**
       * Encrypted payload.
       */
    case GNUNET_MESSAGE_TYPE_MESH_ENCRYPTED:
      t = "ENCRYPTED";
      break;

      /**
       * Local payload traffic
       */
    case GNUNET_MESSAGE_TYPE_MESH_LOCAL_DATA:
      t = "LOCAL_DATA";
      break;

      /**
       * Local ACK for data.
       */
    case GNUNET_MESSAGE_TYPE_MESH_LOCAL_ACK:
      t = "LOCAL_ACK";
      break;

      /**
       * Local monitoring of service.
       */
    case GNUNET_MESSAGE_TYPE_MESH_LOCAL_NACK:
      t = "LOCAL_NACK";
      break;

      /**
       * Local monitoring of service.
       */
    case GNUNET_MESSAGE_TYPE_MESH_LOCAL_INFO_TUNNELS:
      t = "LOCAL_INFO_TUNNELS";
      break;

      /**
       * Local monitoring of service.
       */
    case GNUNET_MESSAGE_TYPE_MESH_LOCAL_INFO_TUNNEL:
      t = "LOCAL_INFO_TUNNEL";
      break;

      /**
       * Local information about all connections of service.
       */
    case GNUNET_MESSAGE_TYPE_MESH_LOCAL_INFO_CONNECTIONS:
      t = "LOCAL_INFO_CONNECTIONS";
      break;

      /**
       * Local information of service about a specific connection.
       */
    case GNUNET_MESSAGE_TYPE_MESH_LOCAL_INFO_CONNECTION:
      t = "LOCAL_INFO_CONNECTION";
      break;

      /**
       * Local information about all peers known to the service.
       */
    case GNUNET_MESSAGE_TYPE_MESH_LOCAL_INFO_PEERS:
      t = "LOCAL_INFO_PEERS";
      break;

      /**
       * Local information of service about a specific peer.
       */
    case GNUNET_MESSAGE_TYPE_MESH_LOCAL_INFO_PEER:
      t = "LOCAL_INFO_PEER";
      break;

      /**
       * Traffic (net-cat style) used by the Command Line Interface.
       */
    case GNUNET_MESSAGE_TYPE_MESH_CLI:
      t = "CLI";
      break;

      /**
       * 640kb should be enough for everybody
       */
    case 299:
      t = "RESERVE_END";
      break;

    default:
      sprintf(buf, "%u (UNKNOWN TYPE)", m);
      return buf;
  }
  sprintf(buf, "%31s", t);
  return buf;
}
#else
const char *
GM_m2s (uint16_t m)
{
  return "";
}
#endif
