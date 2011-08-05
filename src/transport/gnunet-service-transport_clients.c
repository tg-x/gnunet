/*
     This file is part of GNUnet.
     (C) 2010,2011 Christian Grothoff (and other contributing authors)

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
 * @file transport/gnunet-service-transport_clients.c
 * @brief plugin management API
 * @author Christian Grothoff
 */
#include "platform.h"
#include "gnunet-service-transport_clients.h"
#include "gnunet-service-transport_hello.h"
#include "gnunet-service-transport_neighbours.h"
#include "gnunet-service-transport.h"
#include "transport.h"

/**
 * How many messages can we have pending for a given client process
 * before we start to drop incoming messages?  We typically should
 * have only one client and so this would be the primary buffer for
  * messages, so the number should be chosen rather generously.
 *
 * The expectation here is that most of the time the queue is large
 * enough so that a drop is virtually never required.  Note that
 * this value must be about as large as 'TOTAL_MSGS' in the
 * 'test_transport_api_reliability.c', otherwise that testcase may
 * fail.
 */
#define MAX_PENDING (128 * 1024)


/**
 * Linked list of messages to be transmitted to the client.  Each
 * entry is followed by the actual message.
 */
struct ClientMessageQueueEntry
{
  /**
   * This is a doubly-linked list.
   */
  struct ClientMessageQueueEntry *next;

  /**
   * This is a doubly-linked list.
   */
  struct ClientMessageQueueEntry *prev;
};


/**
 * Client connected to the transport service.
 */
struct TransportClient
{

  /**
   * This is a doubly-linked list.
   */
  struct TransportClient *next;

  /**
   * This is a doubly-linked list.
   */
  struct TransportClient *prev;

  /**
   * Handle to the client.
   */
  struct GNUNET_SERVER_Client *client;

  /**
   * Linked list of messages yet to be transmitted to
   * the client.
   */
  struct ClientMessageQueueEntry *message_queue_head;

  /**
   * Tail of linked list of messages yet to be transmitted to the
   * client.
   */
  struct ClientMessageQueueEntry *message_queue_tail;

  /**
   * Current transmit request handle.
   */
  struct GNUNET_CONNECTION_TransmitHandle *th;

  /**
   * Length of the list of messages pending for this client.
   */
  unsigned int message_count;

};


/**
 * Head of linked list of all clients to this service.
 */
static struct TransportClient *clients_head;

/**
 * Tail of linked list of all clients to this service.
 */
static struct TransportClient *clients_tail;


/**
 * Find the internal handle associated with the given client handle
 *
 * @param client server's client handle to look up
 * @return internal client handle
 */
static struct TransportClient *
lookup_client (struct GNUNET_SERVER_Client *client)
{
  struct TransportClient *tc;

  tc = clients_head; 
  while (tc != NULL)
    {
      if (tc->client == client)
	return tc;
      tc = tc->next;
    }
  return NULL;
}


/**
 * Create the internal handle for the given server client handle
 *
 * @param client server's client handle to create our internal handle for
 * @return fresh internal client handle
 */
static struct TransportClient *
setup_client (struct GNUNET_SERVER_Client *client)
{
  struct TransportClient *tc;
  
  tc = GNUNET_malloc (sizeof (struct TransportClient));
  tc->client = client;
  GNUNET_CONTAINER_DLL_insert (clients_head,
			       clients_tail,
			       tc);
  return tc;
}


/**
 * Function called to notify a client about the socket being ready to
 * queue more data.  "buf" will be NULL and "size" zero if the socket
 * was closed for writing in the meantime.
 *
 * @param cls closure
 * @param size number of bytes available in buf
 * @param buf where the callee should write the message
 * @return number of bytes written to buf
 */
static size_t
transmit_to_client_callback (void *cls, 
			     size_t size, 
			     void *buf)
{
  struct TransportClient *tc = cls;
  struct ClientMessageQueueEntry *q;
  const struct GNUNET_MessageHeader *msg;
  char *cbuf;
  uint16_t msize;
  size_t tsize;

  tc->th = NULL;
  if (buf == NULL)
    {
#if DEBUG_TRANSPORT 
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "Transmission to client failed, closing connection.\n");
#endif
      return 0;
    }
  cbuf = buf;
  tsize = 0;
  while (NULL != (q = tc->message_queue_head))
    {
      msg = (const struct GNUNET_MessageHeader *) &q[1];
      msize = ntohs (msg->size);
      if (msize + tsize > size)
        break;
#if DEBUG_TRANSPORT
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "Transmitting message of type %u to client.\n",
                  ntohs (msg->type));
#endif
      GNUNET_CONTAINER_DLL_remove (tc->message_queue_head,
				   tc->message_queue_tail,
				   q);
      tc->message_count--;
      memcpy (&cbuf[tsize], 
	      msg, 
	      msize);
      GNUNET_free (q);
      tsize += msize;
    }
  if (NULL != q)
    {
      GNUNET_assert (msize >= sizeof (struct GNUNET_MessageHeader));
      tc->th = GNUNET_SERVER_notify_transmit_ready (tc->client,
						    msize,
						    GNUNET_TIME_UNIT_FOREVER_REL,
						    &transmit_to_client_callback,
						    tc);
      GNUNET_assert (tc->th != NULL);
    }
  return tsize;
}


/**
 * Queue the given message for transmission to the given client
 *
 * @param client target of the message
 * @param msg message to transmit
 * @param may_drop GNUNET_YES if the message can be dropped
 */
static void
unicast (struct TransportClient *tc,
	 const struct GNUNET_MessageHeader *msg,
	 int may_drop)
{
  struct ClientMessageQueueEntry *q;
  uint16_t msize;

  if ( (tc->message_count >= MAX_PENDING) && 
       (GNUNET_YES == may_drop) )
    {
      GNUNET_log (GNUNET_ERROR_TYPE_INFO,
                  _("Dropping message of type %u and size %u, have %u/%u messages pending\n"),
		  ntohs (msg->type),
		  ntohs (msg->size),
                  tc->message_count,
		  MAX_PENDING);
      GNUNET_STATISTICS_update (GST_stats,
				gettext_noop ("# messages dropped due to slow client"),
				1,
				GNUNET_NO);
      return;
    }
  msize = ntohs (msg->size);
  GNUNET_assert (msize >= sizeof (struct GNUNET_MessageHeader));
  q = GNUNET_malloc (sizeof (struct ClientMessageQueueEntry) + msize);
  memcpy (&q[1], msg, msize);
  GNUNET_CONTAINER_DLL_insert_tail (tc->message_queue_head,
				    tc->message_queue_tail,
				    q);
  tc->message_count++;
  if (tc->th != NULL)
    return;
  tc->th = GNUNET_SERVER_notify_transmit_ready (tc->client,
						msize,
						GNUNET_TIME_UNIT_FOREVER_REL,
						&transmit_to_client_callback,
						tc);
  GNUNET_assert (tc->th != NULL);    
}


/**
 * Called whenever a client is disconnected.  Frees our
 * resources associated with that client.
 *
 * @param cls closure
 * @param client identification of the client
 */
static void
client_disconnect_notification (void *cls,
                                struct GNUNET_SERVER_Client *client)
{
  struct TransportClient *tc;
  struct ClientMessageQueueEntry *mqe;

  if (client == NULL)
    return;
  tc = lookup_client (client);
  if (tc == NULL)
    return;
#if DEBUG_TRANSPORT
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG | GNUNET_ERROR_TYPE_BULK,
              "Client disconnected, cleaning up.\n");
#endif
  while (NULL != (mqe = tc->message_queue_head))
    {
      GNUNET_CONTAINER_DLL_remove (tc->message_queue_head,
				   tc->message_queue_tail,
				   mqe);
      tc->message_count--;
      GNUNET_free (mqe);
    }
  GNUNET_CONTAINER_DLL_remove (clients_head,
			       clients_tail,
			       tc);
  if (tc->th != NULL)
    {
      GNUNET_CONNECTION_notify_transmit_ready_cancel (tc->th);
      tc->th = NULL;
    }
  GNUNET_break (0 == tc->message_count);
  GNUNET_free (tc);
}


/**
 * Start handling requests from clients.
 *
 * @param server server used to accept clients from.
 */
void 
GST_clients_start (struct GNUNET_SERVER_Handle *server)
{
  GNUNET_SERVER_disconnect_notify (server,
                                   &client_disconnect_notification, NULL);
}


/**
 * Stop processing clients.
 */
void
GST_clients_stop ()
{
  /* nothing to do */
}


/**
 * Function called for each of our connected neighbours.  Notify the
 * client about the existing neighbour.
 *
 * @param cls the 'struct TransportClient' to notify
 * @param peer identity of the neighbour
 * @param ats performance data
 * @param ats_count number of entries in ats (excluding 0-termination)
 */
static void
notify_client_about_neighbour (void *cls,
			       const struct GNUNET_PeerIdentity *peer,
			       const struct GNUNET_TRANSPORT_ATS_Information *ats,
			       uint32_t ats_count)
{
  struct TransportClient *tc = cls;
  struct ConnectInfoMessage *cim;
  size_t size;

  size  = sizeof (struct ConnectInfoMessage) + ats_count * sizeof (struct GNUNET_TRANSPORT_ATS_Information);
  GNUNET_assert (size < GNUNET_SERVER_MAX_MESSAGE_SIZE);
  cim = GNUNET_malloc (size);
  cim->header.size = htons (size);
  cim->header.type = htons (GNUNET_MESSAGE_TYPE_TRANSPORT_CONNECT);
  cim->ats_count = htonl(ats_count);
  cim->id = *peer;
  memcpy (&cim->ats,
	  ats, 
	  ats_count * sizeof (struct GNUNET_TRANSPORT_ATS_Information));
  unicast (tc, &cim->header, GNUNET_NO);
  GNUNET_free (cim);
}


/**
 * Initialize a normal client.  We got a start message from this
 * client, add him to the list of clients for broadcasting of inbound
 * messages.
 *
 * @param cls unused
 * @param client the client
 * @param message the start message that was sent
 */
void
GST_clients_handle_start (void *cls,
			  struct GNUNET_SERVER_Client *client,
			  const struct GNUNET_MessageHeader *message)
{
  const struct StartMessage *start;
  struct TransportClient *tc;

  tc = lookup_client (client);
  if (tc != NULL)
    {
      /* got 'start' twice from the same client, not allowed */
      GNUNET_break (0);
      GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
      return;
    }
  start = (const struct StartMessage*) message;
  if ( (GNUNET_NO != ntohl (start->do_check)) &&
       (0 != memcmp (&start->self,
		     &GST_my_identity,
		     sizeof (struct GNUNET_PeerIdentity))) )
    {
      /* client thinks this is a different peer, reject */
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
		  _("Rejecting control connection from peer `%s', which is not me!\n"),
		  GNUNET_i2s (&start->self));
      GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
      return;
    }  
  tc = setup_client (client);
  unicast (tc, GST_hello_get(), GNUNET_NO);
  GST_neighbours_iterate (&notify_client_about_neighbour, tc);
  GNUNET_SERVER_receive_done (client, GNUNET_OK);
}


/**
 * Client asked for transmission to a peer.  Process the request.
 *
 * @param cls unused
 * @param client the client
 * @param message the send message that was sent
 */
void
GST_clients_handle_send (void *cls,
			 struct GNUNET_SERVER_Client *client,
			 const struct GNUNET_MessageHeader *message)
{
}


/**
 * Client asked for a quota change for a particular peer.  Process the request.
 *
 * @param cls unused
 * @param client the client
 * @param message the quota changing message
 */
void
GST_clients_handle_set_quota (void *cls,
			      struct GNUNET_SERVER_Client *client,
			      const struct GNUNET_MessageHeader *message)
{
  const struct QuotaSetMessage *qsm;

  qsm = (const struct QuotaSetMessage *) message;
  GNUNET_STATISTICS_update (GST_stats,
			    gettext_noop ("# SET QUOTA messages received"),
			    1,
			    GNUNET_NO);
#if DEBUG_TRANSPORT 
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Received `%s' request (new quota %u) from client for peer `%4s'\n",
              "SET_QUOTA",
	      (unsigned int) ntohl (qsm->quota.value__),
	      GNUNET_i2s (&qsm->peer));
#endif
  GST_neighbours_set_incoming_quota (&qsm->peer,
				     qsm->quota);
  GNUNET_SERVER_receive_done (client, GNUNET_OK);
}


/**
 * Client asked to resolve an address.  Process the request.
 *
 * @param cls unused
 * @param client the client
 * @param message the resolution request
 */
void
GST_clients_handle_address_lookup (void *cls,
				   struct GNUNET_SERVER_Client *client,
				   const struct GNUNET_MessageHeader *message)
{
}


/**
 * Client asked to obtain information about a peer's addresses.
 * Process the request.
 *
 * @param cls unused
 * @param client the client
 * @param message the peer address information request
 */
void
GST_clients_handle_peer_address_lookup (void *cls,
					struct GNUNET_SERVER_Client *client,
					const struct GNUNET_MessageHeader *message)
{
}


/**
 * Client asked to obtain information about all addresses.
 * Process the request.
 *
 * @param cls unused
 * @param client the client
 * @param message the peer address information request
 */
void
GST_clients_handle_address_iterate (void *cls,
				    struct GNUNET_SERVER_Client *client,
				    const struct GNUNET_MessageHeader *message)
{
}


/**
 * Broadcast the given message to all of our clients.
 *
 * @param msg message to broadcast
 * @param may_drop GNUNET_YES if the message can be dropped
 */
void
GST_clients_broadcast (const struct GNUNET_MessageHeader *msg,
		       int may_drop)
{
  struct TransportClient *tc;

  for (tc = clients_head; tc != NULL; tc = tc->next)
    unicast (tc, msg, may_drop);
}


/**
 * Send the given message to a particular client
 *
 * @param client target of the message
 * @param msg message to transmit
 * @param may_drop GNUNET_YES if the message can be dropped
 */
void
GST_clients_unicast (struct GNUNET_SERVER_Client *client,
		     const struct GNUNET_MessageHeader *msg,
		     int may_drop)
{
  struct TransportClient *tc;

  tc = lookup_client (client);
  if (NULL == tc)
    tc = setup_client (client);
  unicast (tc, msg, may_drop);
}


/* end of file gnunet-service-transport_clients.c */
