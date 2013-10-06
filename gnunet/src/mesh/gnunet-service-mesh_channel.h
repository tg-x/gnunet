/*
     This file is part of GNUnet.
     (C) 2013 Christian Grothoff (and other contributing authors)

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
 * @file mesh/gnunet-service-mesh_channel.h
 * @brief mesh service; dealing with end-to-end channels
 * @author Bartlomiej Polot
 *
 * All functions in this file should use the prefix GMCH (Gnunet Mesh CHannel)
 */

#ifndef GNUNET_SERVICE_MESH_CHANNEL_H
#define GNUNET_SERVICE_MESH_CHANNEL_H

#ifdef __cplusplus
extern "C"
{
#if 0                           /* keep Emacsens' auto-indent happy */
}
#endif
#endif

#include "gnunet_util_lib.h"

/**
 * Struct containing all information regarding a channel to a remote client.
 */
struct MeshChannel;

/**
 * Send an end-to-end ACK message for the most recent in-sequence payload.
 *
 * If channel is not reliable, do nothing.
 *
 * @param ch Channel this is about.
 * @param fwd Is for FWD traffic? (ACK dest->owner)
 */
void
GMCH_send_ack (struct MeshChannel *ch, int fwd);

/**
 * Send data on a channel.
 *
 * If the destination is local, send it to client, otherwise encrypt and
 * send to next hop.
 *
 * @param ch Channel
 * @param msg Message.
 * @param fwd Is this a fwd (root->dest) message?
 */
void
GMCH_send_data (struct MeshChannel *ch,
                const struct GNUNET_MESH_Data *msg,
                int fwd);

/**
 * Notify the destination client that a new incoming channel was created.
 *
 * @param ch Channel that was created.
 */
void
GMCH_send_channel_create (struct MeshChannel *ch);


/**
 * Notify a client that the channel is no longer valid.
 *
 * @param ch Channel that is destroyed.
 * @param fwd Forward notification (owner->dest)?
 */
void
GMCH_send_channel_destroy (struct MeshChannel *ch, int fwd);


/**
 * Log channel info.
 *
 * @param ch Channel.
 */
void
GMCH_debug (struct MeshChannel *ch);

#if 0                           /* keep Emacsens' auto-indent happy */
{
#endif
#ifdef __cplusplus
}
#endif

/* ifndef GNUNET_SERVICE_MESH_CHANNEL_H */
#endif
/* end of gnunet-service-mesh_channel.h */