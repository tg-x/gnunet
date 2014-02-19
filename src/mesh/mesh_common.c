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
  switch (m)
    {
      /**
       * Request the creation of a path
       */
    case 256: return "GNUNET_MESSAGE_TYPE_MESH_CONNECTION_CREATE";

      /**
       * Request the modification of an existing path
       */
    case 257: return "GNUNET_MESSAGE_TYPE_MESH_CONNECTION_ACK";

      /**
       * Notify that a connection of a path is no longer valid
       */
    case 258: return "GNUNET_MESSAGE_TYPE_MESH_CONNECTION_BROKEN";

      /**
       * At some point, the route will spontaneously change
       */
    case 259: return "GNUNET_MESSAGE_TYPE_MESH_PATH_CHANGED";

      /**
       * Transport payload data.
       */
    case 260: return "GNUNET_MESSAGE_TYPE_MESH_DATA";

    /**
     * Confirm receipt of payload data.
     */
    case 261: return "GNUNET_MESSAGE_TYPE_MESH_DATA_ACK";

      /**
       * Key exchange encapsulation.
       */
    case 262: return "GNUNET_MESSAGE_TYPE_MESH_KX";

      /**
       * New ephemeral key.
       */
    case 263: return "GNUNET_MESSAGE_TYPE_MESH_KX_EPHEMERAL";

      /**
       * Challenge to test peer's session key.
       */
    case 264: return "GNUNET_MESSAGE_TYPE_MESH_KX_PING";

      /**
       * Answer to session key challenge.
       */
    case 265: return "GNUNET_MESSAGE_TYPE_MESH_KX_PONG";

      /**
       * Request the destuction of a path
       */
    case 266: return "GNUNET_MESSAGE_TYPE_MESH_CONNECTION_DESTROY";

      /**
       * ACK for a data packet.
       */
    case 268: return "GNUNET_MESSAGE_TYPE_MESH_ACK";

      /**
       * POLL for ACK.
       */
    case 269: return "GNUNET_MESSAGE_TYPE_MESH_POLL";

      /**
       * Announce origin is still alive.
       */
    case 270: return "GNUNET_MESSAGE_TYPE_MESH_KEEPALIVE";

    /**
       * Connect to the mesh service, specifying subscriptions
       */
    case 272: return "GNUNET_MESSAGE_TYPE_MESH_LOCAL_CONNECT";

      /**
       * Ask the mesh service to create a new tunnel
       */
    case 273: return "GNUNET_MESSAGE_TYPE_MESH_CHANNEL_CREATE";

      /**
       * Ask the mesh service to destroy a tunnel
       */
    case 274: return "GNUNET_MESSAGE_TYPE_MESH_CHANNEL_DESTROY";

      /**
       * Confirm the creation of a channel.
       */
    case 275: return "GNUNET_MESSAGE_TYPE_MESH_CHANNEL_ACK";

      /**
       * Confirm the creation of a channel.
       */
    case 276: return "GNUNET_MESSAGE_TYPE_MESH_CHANNEL_NACK";

      /**
       * Encrypted payload.
       */
    case 280: return "GNUNET_MESSAGE_TYPE_MESH_ENCRYPTED";

      /**
       * Local payload traffic
       */
    case 285: return "GNUNET_MESSAGE_TYPE_MESH_LOCAL_DATA";

      /**
       * Local ACK for data.
       */
    case 286: return "GNUNET_MESSAGE_TYPE_MESH_LOCAL_ACK";

      /**
       * Local monitoring of service.
       */
    case 287: return "GNUNET_MESSAGE_TYPE_MESH_LOCAL_NACK";

      /**
       * Local monitoring of service.
       */
    case 292: return "GNUNET_MESSAGE_TYPE_MESH_LOCAL_INFO_TUNNELS";

      /**
       * Local monitoring of service.
       */
    case 293: return "GNUNET_MESSAGE_TYPE_MESH_LOCAL_INFO_TUNNEL";

      /**
       * Local information about all connections of service.
       */
    case 294: return "GNUNET_MESSAGE_TYPE_MESH_LOCAL_INFO_CONNECTIONS";

      /**
       * Local information of service about a specific connection.
       */
    case 295: return "GNUNET_MESSAGE_TYPE_MESH_LOCAL_INFO_CONNECTION";

      /**
       * Local information about all peers known to the service.
       */
      case 296: return "GNUNET_MESSAGE_TYPE_MESH_LOCAL_INFO_PEERS";

      /**
       * Local information of service about a specific peer.
       */
    case 297: return "GNUNET_MESSAGE_TYPE_MESH_LOCAL_INFO_PEER";

      /**
       * Traffic (net-cat style) used by the Command Line Interface.
       */
    case 298: return "GNUNET_MESSAGE_TYPE_MESH_CLI";

      /**
       * 640kb should be enough for everybody
       */
    case 299: return "GNUNET_MESSAGE_TYPE_MESH_RESERVE_END";
    }
  sprintf(buf, "%u (UNKNOWN TYPE)", m);
  return buf;
}
#else
const char *
GM_m2s (uint16_t m)
{
  return "";
}
#endif