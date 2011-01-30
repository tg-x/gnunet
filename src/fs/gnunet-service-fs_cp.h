/*
     This file is part of GNUnet.
     (C) 2011 Christian Grothoff (and other contributing authors)

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
 * @file fs/gnunet-service-fs_cp.h
 * @brief API to handle 'connected peers'
 * @author Christian Grothoff
 */
#ifndef GNUNET_SERVICE_FS_CP_H
#define GNUNET_SERVICE_FS_CP_H

#include "gnunet-service-fs.h"


/**
 * Performance data kept for a peer.
 */
struct GSF_PeerPerformanceData
{

  /**
   * Transport performance data.
   */
  struct GNUNET_TRANSPORT_ATS_Information *atsi;

  /**
   * List of the last clients for which this peer successfully
   * answered a query.
   */
  struct GSF_LocalClient *last_client_replies[CS2P_SUCCESS_LIST_SIZE];

  /**
   * List of the last PIDs for which
   * this peer successfully answered a query;
   * We use 0 to indicate no successful reply.
   */
  GNUNET_PEER_Id last_p2p_replies[P2P_SUCCESS_LIST_SIZE];

  /**
   * Average delay between sending the peer a request and
   * getting a reply (only calculated over the requests for
   * which we actually got a reply).   Calculated
   * as a moving average: new_delay = ((n-1)*last_delay+curr_delay) / n
   */ 
  struct GNUNET_TIME_Relative avg_reply_delay;

  /**
   * Point in time until which this peer does not want us to migrate content
   * to it.
   */
  struct GNUNET_TIME_Absolute migration_blocked_until;

  /**
   * Transmission times for the last MAX_QUEUE_PER_PEER
   * requests for this peer.  Used as a ring buffer, current
   * offset is stored in 'last_request_times_off'.  If the
   * oldest entry is more recent than the 'avg_delay', we should
   * not send any more requests right now.
   */
  struct GNUNET_TIME_Absolute last_request_times[MAX_QUEUE_PER_PEER];

  /**
   * How long does it typically take for us to transmit a message
   * to this peer?  (delay between the request being issued and
   * the callback being invoked).
   */
  struct GNUNET_LOAD_Value *transmission_delay;

  /**
   * Average priority of successful replies.  Calculated
   * as a moving average: new_avg = ((n-1)*last_avg+curr_prio) / n
   */
  double avg_priority;

  /**
   * Number of pending queries (replies are not counted)
   */
  unsigned int pending_queries;

  /**
   * Number of pending replies (queries are not counted)
   */
  unsigned int pending_replies;

};


/**
 * Signature of function called on a connected peer.
 *
 * @param cls closure
 * @param peer identity of the peer
 * @param cp handle to the connected peer record
 * @param perf peer performance data
 */
typedef void (*GSF_ConnectedPeerIterator)(void *cls,
					  const struct GNUNET_PeerIdentity *peer,
					  struct GSF_ConnectedPeer *cp,
					  const struct GSF_PeerPerformanceData *ppd);


/**
 * Function called to get a message for transmission.
 *
 * @param cls closure
 * @param buf_size number of bytes available in buf
 * @param buf where to copy the message, NULL on error (peer disconnect)
 * @return number of bytes copied to 'buf', can be 0 (without indicating an error)
 */
typedef size_t (*GSF_GetMessageCallback)(void *cls,
					 size_t buf_size,
					 void *buf);


/**
 * Signature of function called on a reservation success or failure.
 *
 * @param cls closure
 * @param cp handle to the connected peer record
 * @param success GNUNET_YES on success, GNUNET_NO on failure
 */
typedef void (*GSF_PeerReserveCallback)(void *cls,
					struct GSF_ConnectedPeer *cp,
					int success);


/**
 * Handle to cancel a transmission request.
 */
struct GSF_PeerTransmitHandle;


/**
 * A peer connected to us.  Setup the connected peer
 * records.
 *
 * @param peer identity of peer that connected
 * @param atsi performance data for the connection
 * @return handle to connected peer entry
 */
struct GSF_ConnectedPeer *
GSF_peer_connect_handler_ (const struct GNUNET_PeerIdentity *peer,
			   const struct GNUNET_TRANSPORT_ATS_Information *atsi);


/**
 * Transmit a message to the given peer as soon as possible.
 * If the peer disconnects before the transmission can happen,
 * the callback is invoked with a 'NULL' buffer.
 *
 * @param peer target peer
 * @param is_query is this a query (GNUNET_YES) or content (GNUNET_NO)
 * @param priority how important is this request?
 * @param timeout when does this request timeout (call gmc with error)
 * @param size number of bytes we would like to send to the peer
 * @param gmc function to call to get the message
 * @param gmc_cls closure for gmc
 * @return handle to cancel request
 */
struct GSF_PeerTransmitHandle *
GSF_peer_transmit_ (struct GSF_ConnectedPeer *peer,
		    int is_query,
		    uint32_t priority,
		    struct GNUNET_TIME_Relative timeout,
		    size_t size,
		    GSF_GetMessageCallback gmc,
		    void *gmc_cls);


/**
 * Cancel an earlier request for transmission.
 */
void
GSF_peer_transmit_cancel_ (struct GSF_PeerTransmitHandle *pth);


/**
 * Report on receiving a reply; update the performance record of the given peer.
 *
 * @param peer responding peer (will be updated)
 * @param request_time time at which the original query was transmitted
 * @param request_priority priority of the original request
 * @param initiator_client local client on responsible for query (or NULL)
 * @param initiator_peer other peer responsible for query (or NULL)
 */
void
GSF_peer_update_performance_ (struct GSF_ConnectedPeer *peer,
			      GNUNET_TIME_Absolute request_time,
			      uint32_t request_priority,
			      const struct GSF_LocalClient *initiator_client,
			      const struct GSF_ConnectedPeer *initiator_peer);


/**
 * Method called whenever a given peer has a status change.
 *
 * @param cls closure
 * @param peer peer identity this notification is about
 * @param bandwidth_in available amount of inbound bandwidth
 * @param bandwidth_out available amount of outbound bandwidth
 * @param timeout absolute time when this peer will time out
 *        unless we see some further activity from it
 * @param atsi status information
 */
void
GSF_peer_status_handler_ (void *cls,
			  const struct GNUNET_PeerIdentity *peer,
			  struct GNUNET_BANDWIDTH_Value32NBO bandwidth_in,
			  struct GNUNET_BANDWIDTH_Value32NBO bandwidth_out,
			  struct GNUNET_TIME_Absolute timeout,
			  const struct GNUNET_TRANSPORT_ATS_Information *atsi);


/**
 * A peer disconnected from us.  Tear down the connected peer
 * record.
 *
 * @param cls unused
 * @param peer identity of peer that connected
 */
void
GSF_peer_disconnect_handler_ (void *cls,
			      const struct GNUNET_PeerIdentity *peer);


/**
 * Notification that a local client disconnected.  Clean up all of our
 * references to the given handle.
 *
 * @param lc handle to the local client (henceforth invalid)
 */
void
GSF_handle_local_client_disconnect_ (const struct GSF_LocalClient *lc);


/**
 * Iterate over all connected peers.
 *
 * @param it function to call for each peer
 * @param it_cls closure for it
 */
void
GSF_iterate_connected_peers_ (GSF_ConnectedPeerIterator it,
			      void *it_cls);


// FIXME: should we allow queueing multiple reservation requests?
// FIXME: what about cancellation?
// FIXME: change docu on peer disconnect handling?
/**
 * Try to reserve bandwidth (to receive data FROM the given peer).
 * This function must only be called ONCE per connected peer at a
 * time; it can be called again after the 'rc' callback was invoked.
 * If the peer disconnects, the request is (silently!) ignored (and
 * the requester is responsible to register for notification about the
 * peer disconnect if any special action needs to be taken in this
 * case).
 *
 * @param cp peer to reserve bandwidth from
 * @param size number of bytes to reserve
 * @param rc function to call upon reservation success
 * @param rc_cls closure for rc
 */
void
GSF_connected_peer_reserve_ (struct GSF_ConnectedPeer *cp,
			     size_t size,
			     GSF_PeerReserveCallback rc,
			     void *rc_cls);


#endif
/* end of gnunet-service-fs_cp.h */
