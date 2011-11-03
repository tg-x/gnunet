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
#include "gnunet-service-transport_blacklist.h"
#include "gnunet-service-transport_clients.h"
#include "gnunet-service-transport_hello.h"
#include "gnunet-service-transport_neighbours.h"
#include "gnunet-service-transport_plugins.h"
#include "gnunet-service-transport_validation.h"
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

  /**
   * Is this client interested in payload messages?
   */
  int send_payload;
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

  GNUNET_assert (lookup_client (client) == NULL);
  tc = GNUNET_malloc (sizeof (struct TransportClient));
  tc->client = client;
  GNUNET_CONTAINER_DLL_insert (clients_head, clients_tail, tc);

#if DEBUG_TRANSPORT
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
               "Client %X connected\n", tc);
#endif
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
transmit_to_client_callback (void *cls, size_t size, void *buf)
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
                "Transmitting message of type %u to client %X.\n",
                ntohs (msg->type), tc);
#endif
    GNUNET_CONTAINER_DLL_remove (tc->message_queue_head, tc->message_queue_tail,
                                 q);
    tc->message_count--;
    memcpy (&cbuf[tsize], msg, msize);
    GNUNET_free (q);
    tsize += msize;
  }
  if (NULL != q)
  {
    GNUNET_assert (msize >= sizeof (struct GNUNET_MessageHeader));
    tc->th =
        GNUNET_SERVER_notify_transmit_ready (tc->client, msize,
                                             GNUNET_TIME_UNIT_FOREVER_REL,
                                             &transmit_to_client_callback, tc);
    GNUNET_assert (tc->th != NULL);
  }
  return tsize;
}


/**
 * Queue the given message for transmission to the given client
 *
 * @param tc target of the message
 * @param msg message to transmit
 * @param may_drop GNUNET_YES if the message can be dropped
 */
static void
unicast (struct TransportClient *tc, const struct GNUNET_MessageHeader *msg,
         int may_drop)
{
  struct ClientMessageQueueEntry *q;
  uint16_t msize;

  if ((tc->message_count >= MAX_PENDING) && (GNUNET_YES == may_drop))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_INFO,
                _
                ("Dropping message of type %u and size %u, have %u/%u messages pending\n"),
                ntohs (msg->type), ntohs (msg->size), tc->message_count,
                MAX_PENDING);
    GNUNET_STATISTICS_update (GST_stats,
                              gettext_noop
                              ("# messages dropped due to slow client"), 1,
                              GNUNET_NO);
    return;
  }
  msize = ntohs (msg->size);
  GNUNET_assert (msize >= sizeof (struct GNUNET_MessageHeader));
  q = GNUNET_malloc (sizeof (struct ClientMessageQueueEntry) + msize);
  memcpy (&q[1], msg, msize);
  GNUNET_CONTAINER_DLL_insert_tail (tc->message_queue_head,
                                    tc->message_queue_tail, q);
  tc->message_count++;
  if (tc->th != NULL)
    return;
  tc->th =
      GNUNET_SERVER_notify_transmit_ready (tc->client, msize,
                                           GNUNET_TIME_UNIT_FOREVER_REL,
                                           &transmit_to_client_callback, tc);
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
client_disconnect_notification (void *cls, struct GNUNET_SERVER_Client *client)
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
              "Client %X disconnected, cleaning up.\n", tc);
#endif
  while (NULL != (mqe = tc->message_queue_head))
  {
    GNUNET_CONTAINER_DLL_remove (tc->message_queue_head, tc->message_queue_tail,
                                 mqe);
    tc->message_count--;
    GNUNET_free (mqe);
  }
  GNUNET_CONTAINER_DLL_remove (clients_head, clients_tail, tc);
  if (tc->th != NULL)
  {
    GNUNET_CONNECTION_notify_transmit_ready_cancel (tc->th);
    tc->th = NULL;
  }
  GNUNET_break (0 == tc->message_count);
  GNUNET_free (tc);
}


/**
 * Function called for each of our connected neighbours.  Notify the
 * client about the existing neighbour.
 *
 * @param cls the 'struct TransportClient' to notify
 * @param peer identity of the neighbour
 * @param ats performance data
 * @param ats_count number of entries in ats (excluding 0-termination)
 * @param transport plugin
 * @param addr address
 * @param addrlen address length
 */
static void
notify_client_about_neighbour (void *cls,
                               const struct GNUNET_PeerIdentity *peer,
                               const struct GNUNET_ATS_Information
                               *ats, uint32_t ats_count,
                               const char * transport,
                               const void * addr,
                               size_t addrlen)
{
  struct TransportClient *tc = cls;
  struct ConnectInfoMessage *cim;
  struct GNUNET_ATS_Information *ap;
  size_t size =
    sizeof (struct ConnectInfoMessage) +
    ats_count * sizeof (struct GNUNET_ATS_Information);
  char buf[size];

  GNUNET_assert (size < GNUNET_SERVER_MAX_MESSAGE_SIZE);
  cim = (struct ConnectInfoMessage*) buf;
  cim->header.size = htons (size);
  cim->header.type = htons (GNUNET_MESSAGE_TYPE_TRANSPORT_CONNECT);
  cim->ats_count = htonl (ats_count);
  cim->id = *peer;
  ap = (struct GNUNET_ATS_Information *) &cim[1];
  memcpy (ap, ats,
          ats_count * sizeof (struct GNUNET_ATS_Information));
  unicast (tc, &cim->header, GNUNET_NO);
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
static void
clients_handle_start (void *cls, struct GNUNET_SERVER_Client *client,
                      const struct GNUNET_MessageHeader *message)
{
  const struct StartMessage *start;
  struct TransportClient *tc;
  uint32_t options;

  tc = lookup_client (client);

#if DEBUG_TRANSPORT
  if (tc != NULL)
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG | GNUNET_ERROR_TYPE_BULK,
                "Client %X sent START\n", tc);
  else
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG | GNUNET_ERROR_TYPE_BULK,
                "Client %X sent START\n", tc);
#endif
  if (tc != NULL)
  {
    /* got 'start' twice from the same client, not allowed */
#if DEBUG_TRANSPORT
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG | GNUNET_ERROR_TYPE_BULK,
                "TransportClient %X ServerClient %X  sent multiple START messages\n",
                tc, tc->client);
#endif
    GNUNET_break (0);
    GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
    return;
  }
  start = (const struct StartMessage *) message;
  options = ntohl (start->options);
  if ((0 != (1 & options) ) &&
      (0 !=
       memcmp (&start->self, &GST_my_identity,
	       sizeof (struct GNUNET_PeerIdentity))))
  {
    /* client thinks this is a different peer, reject */
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                _
                ("Rejecting control connection from peer `%s', which is not me!\n"),
                GNUNET_i2s (&start->self));
    GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
    return;
  }
  tc = setup_client (client);
  tc->send_payload = (0 != (2 & options));
  unicast (tc, GST_hello_get (), GNUNET_NO);
  GST_neighbours_iterate (&notify_client_about_neighbour, tc);
  GNUNET_SERVER_receive_done (client, GNUNET_OK);
}


/**
 * Client sent us a HELLO.  Process the request.
 *
 * @param cls unused
 * @param client the client
 * @param message the HELLO message
 */
static void
clients_handle_hello (void *cls, struct GNUNET_SERVER_Client *client,
                      const struct GNUNET_MessageHeader *message)
{
  GST_validation_handle_hello (message);
  GNUNET_SERVER_receive_done (client, GNUNET_OK);
}


/**
 * Closure for 'handle_send_transmit_continuation'
 */
struct SendTransmitContinuationContext
{
  /**
   * Client that made the request.
   */
  struct GNUNET_SERVER_Client *client;

  /**
   * Peer that was the target.
   */
  struct GNUNET_PeerIdentity target;
};


/**
 * Function called after the transmission is done.  Notify the client that it is
 * OK to send the next message.
 *
 * @param cls closure
 * @param success GNUNET_OK on success, GNUNET_NO on failure, GNUNET_SYSERR if we're not connected
 */
static void
handle_send_transmit_continuation (void *cls, int success)
{
  struct SendTransmitContinuationContext *stcc = cls;
  struct SendOkMessage send_ok_msg;

  send_ok_msg.header.size = htons (sizeof (send_ok_msg));
  send_ok_msg.header.type = htons (GNUNET_MESSAGE_TYPE_TRANSPORT_SEND_OK);
  send_ok_msg.success = htonl (success);
  send_ok_msg.latency =
      GNUNET_TIME_relative_hton (GNUNET_TIME_UNIT_FOREVER_REL);
  send_ok_msg.peer = stcc->target;
  GST_clients_unicast (stcc->client, &send_ok_msg.header, GNUNET_NO);
  GNUNET_SERVER_client_drop (stcc->client);
  GNUNET_free (stcc);
}


/**
 * Client asked for transmission to a peer.  Process the request.
 *
 * @param cls unused
 * @param client the client
 * @param message the send message that was sent
 */
static void
clients_handle_send (void *cls, struct GNUNET_SERVER_Client *client,
                     const struct GNUNET_MessageHeader *message)
{
  const struct OutboundMessage *obm;
  const struct GNUNET_MessageHeader *obmm;
  struct SendTransmitContinuationContext *stcc;
  uint16_t size;
  uint16_t msize;
  struct TransportClient *tc;

  tc = lookup_client (client);
  if (NULL == tc)
  {
    /* client asked for transmission before 'START' */
    GNUNET_break (0);
    GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
    return;
  }

  size = ntohs (message->size);
  if (size <
      sizeof (struct OutboundMessage) + sizeof (struct GNUNET_MessageHeader))
  {
    GNUNET_break (0);
    GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
    return;
  }
  obm = (const struct OutboundMessage *) message;
  obmm = (const struct GNUNET_MessageHeader *) &obm[1];
  msize = size - sizeof (struct OutboundMessage);
  if (msize < sizeof (struct GNUNET_MessageHeader))
  {
    GNUNET_break (0);
    GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
    return;
  }
  GNUNET_STATISTICS_update (GST_stats,
                            gettext_noop
                            ("# bytes payload received for other peers"), msize,
                            GNUNET_NO);
#if DEBUG_TRANSPORT
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Received `%s' request from client with target `%4s' and first message of type %u and total size %u\n",
              "SEND", GNUNET_i2s (&obm->peer), ntohs (obmm->type), msize);
#endif
  if (GNUNET_NO == GST_neighbours_test_connected (&obm->peer))
  {
    /* not connected, not allowed to send; can happen due to asynchronous operations */
#if DEBUG_TRANSPORT
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "Could not send message to peer `%s': not connected\n",
                GNUNET_i2s (&obm->peer));
#endif
    GNUNET_STATISTICS_update (GST_stats,
                              gettext_noop
                              ("# bytes payload dropped (other peer was not connected)"),
                              msize, GNUNET_NO);
    GNUNET_SERVER_receive_done (client, GNUNET_OK);
    return;
  }
  GNUNET_SERVER_receive_done (client, GNUNET_OK);
  stcc = GNUNET_malloc (sizeof (struct SendTransmitContinuationContext));
  stcc->target = obm->peer;
  stcc->client = client;
  GNUNET_SERVER_client_keep (client);
  GST_neighbours_send (&obm->peer, obmm, msize,
                       GNUNET_TIME_relative_ntoh (obm->timeout),
                       &handle_send_transmit_continuation, stcc);
}


/**
 * Try to initiate a connection to the given peer if the blacklist
 * allowed it.
 *
 * @param cls closure (unused, NULL)
 * @param peer identity of peer that was tested
 * @param result GNUNET_OK if the connection is allowed,
 *               GNUNET_NO if not
 */
static void
try_connect_if_allowed (void *cls, const struct GNUNET_PeerIdentity *peer,
                        int result)
{
  if (GNUNET_OK != result)
    return;                     /* not allowed */
  GST_neighbours_try_connect (peer);
}


/**
 * Handle request connect message
 *
 * @param cls closure (always NULL)
 * @param client identification of the client
 * @param message the actual message
 */
static void
clients_handle_request_connect (void *cls, struct GNUNET_SERVER_Client *client,
                                const struct GNUNET_MessageHeader *message)
{
  const struct TransportRequestConnectMessage *trcm =
      (const struct TransportRequestConnectMessage *) message;

  GNUNET_STATISTICS_update (GST_stats,
                            gettext_noop
                            ("# REQUEST CONNECT messages received"), 1,
                            GNUNET_NO);
#if DEBUG_TRANSPORT
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Received a request connect message for peer `%s'\n",
              GNUNET_i2s (&trcm->peer));
#endif
  (void) GST_blacklist_test_allowed (&trcm->peer, NULL, &try_connect_if_allowed,
                                     NULL);
  GNUNET_SERVER_receive_done (client, GNUNET_OK);
}


/**
 * Take the given address and append it to the set of results sent back to
 * the client.
 *
 * @param cls the transmission context used ('struct GNUNET_SERVER_TransmitContext*')
 * @param address the resolved name, NULL to indicate the last response
 */
static void
transmit_address_to_client (void *cls, const char *buf)
{
  struct GNUNET_SERVER_TransmitContext *tc = cls;

  if (NULL == buf)
  {
    GNUNET_SERVER_transmit_context_append_data (tc, NULL, 0,
                                                GNUNET_MESSAGE_TYPE_TRANSPORT_ADDRESS_REPLY);
    GNUNET_SERVER_transmit_context_run (tc, GNUNET_TIME_UNIT_FOREVER_REL);
    return;
  }
  GNUNET_SERVER_transmit_context_append_data (tc, buf, strlen (buf) + 1,
                                              GNUNET_MESSAGE_TYPE_TRANSPORT_ADDRESS_REPLY);
}


/**
 * Take the given address and append it to the set of results sent back to
 * the client.
 *
 * @param cls the transmission context used ('struct GNUNET_SERVER_TransmitContext*')
 * @param address the resolved name, NULL to indicate the last response
 */
static void
transmit_binary_to_client (void *cls, void *buf, size_t size)
{
  struct GNUNET_SERVER_TransmitContext *tc = cls;

  if (NULL == buf)
  {
    GNUNET_SERVER_transmit_context_append_data (tc, NULL, 0,
                                                GNUNET_MESSAGE_TYPE_TRANSPORT_ADDRESS_REPLY);
    GNUNET_SERVER_transmit_context_run (tc, GNUNET_TIME_UNIT_FOREVER_REL);
    return;
  }
  GNUNET_SERVER_transmit_context_append_data (tc, buf, size,
                                              GNUNET_MESSAGE_TYPE_TRANSPORT_ADDRESS_REPLY);
}


/**
 * Client asked to resolve an address.  Process the request.
 *
 * @param cls unused
 * @param client the client
 * @param message the resolution request
 */
static void
clients_handle_address_lookup (void *cls, struct GNUNET_SERVER_Client *client,
                               const struct GNUNET_MessageHeader *message)
{
  const struct AddressLookupMessage *alum;
  struct GNUNET_TRANSPORT_PluginFunctions *papi;
  const char *plugin_name;
  const char *address;
  uint32_t address_len;
  uint16_t size;
  struct GNUNET_SERVER_TransmitContext *tc;
  struct GNUNET_TIME_Relative rtimeout;
  int32_t numeric;

  size = ntohs (message->size);
  if (size < sizeof (struct AddressLookupMessage))
  {
    GNUNET_break (0);
    GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
    return;
  }
  alum = (const struct AddressLookupMessage *) message;
  address_len = ntohl (alum->addrlen);
  if (size <= sizeof (struct AddressLookupMessage) + address_len)
  {
    GNUNET_break (0);
    GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
    return;
  }
  address = (const char *) &alum[1];
  plugin_name = (const char *) &address[address_len];
  if (plugin_name[size - sizeof (struct AddressLookupMessage) - address_len - 1]
      != '\0')
  {
    GNUNET_break (0);
    GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
    return;
  }
  rtimeout = GNUNET_TIME_relative_ntoh (alum->timeout);
  numeric = ntohl (alum->numeric_only);
  tc = GNUNET_SERVER_transmit_context_create (client);
  papi = GST_plugins_find (plugin_name);
  if (NULL == papi)
  {
    GNUNET_SERVER_transmit_context_append_data (tc, NULL, 0,
                                                GNUNET_MESSAGE_TYPE_TRANSPORT_ADDRESS_REPLY);
    GNUNET_SERVER_transmit_context_run (tc, rtimeout);
    return;
  }
  GNUNET_SERVER_disable_receive_done_warning (client);
  papi->address_pretty_printer (papi->cls, plugin_name, address, address_len,
                                numeric, rtimeout, &transmit_address_to_client,
                                tc);
}


/**
 * Send an address to the client.
 *
 * @param cls our 'struct GNUNET_SERVER_TransmitContext' (for sending)
 * @param public_key public key for the peer, never NULL
 * @param target peer this change is about, never NULL
 * @param valid_until until what time do we consider the address valid?
 * @param validation_block  is FOREVER if the address is for an unsupported plugin (from PEERINFO)
 *                          is ZERO if the address is considered valid (no validation needed)
 *                          is a time in the future if we're currently denying re-validation
 * @param plugin_name name of the plugin
 * @param plugin_address binary address
 * @param plugin_address_len length of address
 */
static void
send_address_to_client (void *cls,
                        const struct GNUNET_CRYPTO_RsaPublicKeyBinaryEncoded
                        *public_key, const struct GNUNET_PeerIdentity *target,
                        struct GNUNET_TIME_Absolute valid_until,
                        struct GNUNET_TIME_Absolute validation_block,
                        const char *plugin_name, const void *plugin_address,
                        size_t plugin_address_len)
{
  struct GNUNET_SERVER_TransmitContext *tc = cls;
  char *addr_buf;

  /* FIXME: move to a binary format!!! */
  GNUNET_asprintf (&addr_buf, "%s --- %s, %s",
                   GST_plugins_a2s (plugin_name, plugin_address,
                                    plugin_address_len),
                   (GNUNET_YES ==
                    GST_neighbours_test_connected (target)) ? "CONNECTED" :
                   "DISCONNECTED",
                   (GNUNET_TIME_absolute_get_remaining (valid_until).rel_value >
                    0) ? "VALIDATED" : "UNVALIDATED");
  transmit_address_to_client (tc, addr_buf);
  GNUNET_free (addr_buf);
}


/**
 * Client asked to obtain information about a peer's addresses.
 * Process the request.
 * FIXME: use better name!
 *
 * @param cls unused
 * @param client the client
 * @param message the peer address information request
 */
static void
clients_handle_peer_address_lookup (void *cls,
                                    struct GNUNET_SERVER_Client *client,
                                    const struct GNUNET_MessageHeader *message)
{
  const struct PeerAddressLookupMessage *peer_address_lookup;
  struct GNUNET_SERVER_TransmitContext *tc;

  peer_address_lookup = (const struct PeerAddressLookupMessage *) message;
  GNUNET_break (ntohl (peer_address_lookup->reserved) == 0);
  tc = GNUNET_SERVER_transmit_context_create (client);
  GST_validation_get_addresses (&peer_address_lookup->peer,
                                &send_address_to_client, tc);
  GNUNET_SERVER_transmit_context_append_data (tc, NULL, 0,
                                              GNUNET_MESSAGE_TYPE_TRANSPORT_ADDRESS_REPLY);
  GNUNET_SERVER_transmit_context_run (tc, GNUNET_TIME_UNIT_FOREVER_REL);
}


/**
 * Output the active address of connected neighbours to the given client.
 *
 * @param cls the 'struct GNUNET_SERVER_TransmitContext' for transmission to the client
 * @param neighbour identity of the neighbour
 * @param ats performance data
 * @param ats_count number of entries in ats (excluding 0-termination)
 * @param transport plugin
 * @param addr address
 * @param addrlen address length
 */
static void
output_addresses (void *cls, const struct GNUNET_PeerIdentity *peer,
                  const struct GNUNET_ATS_Information *ats,
                  uint32_t ats_count,
                  const char * transport,
                  const void * addr,
                  size_t addrlen)
{
  struct GNUNET_SERVER_TransmitContext *tc = cls;
  struct AddressIterateResponseMessage * msg;
  size_t size;

  size =
      (sizeof (struct AddressIterateResponseMessage) + strlen(transport) + 1);
  msg = GNUNET_malloc (size);
  memcpy (&msg->peer, peer, sizeof (struct GNUNET_PeerIdentity));
  memcpy (&msg[0], transport, strlen(transport)+1);
  msg->addrlen = ntohs (addrlen);
  msg->pluginlen = ntohs (strlen(transport)+1);

  transmit_binary_to_client (tc, msg, size);
  GNUNET_free(msg);
}


/**
 * Client asked to obtain information about all actively used addresses.
 * Process the request.  FIXME: use better name!
 *
 * @param cls unused
 * @param client the client
 * @param message the peer address information request
 */
static void
clients_handle_address_iterate (void *cls, struct GNUNET_SERVER_Client *client,
                                const struct GNUNET_MessageHeader *message)
{
  struct GNUNET_SERVER_TransmitContext *tc;

  GNUNET_SERVER_disable_receive_done_warning (client);
  tc = GNUNET_SERVER_transmit_context_create (client);
  GST_neighbours_iterate (&output_addresses, tc);
  GNUNET_SERVER_transmit_context_append_data (tc, NULL, 0,
                                              GNUNET_MESSAGE_TYPE_TRANSPORT_ADDRESS_REPLY);
  GNUNET_SERVER_transmit_context_run (tc, GNUNET_TIME_UNIT_FOREVER_REL);
}


/**
 * Start handling requests from clients.
 *
 * @param server server used to accept clients from.
 */
void
GST_clients_start (struct GNUNET_SERVER_Handle *server)
{
  static const struct GNUNET_SERVER_MessageHandler handlers[] = {
    {&clients_handle_start, NULL,
     GNUNET_MESSAGE_TYPE_TRANSPORT_START, sizeof (struct StartMessage)},
    {&clients_handle_hello, NULL,
     GNUNET_MESSAGE_TYPE_HELLO, 0},
    {&clients_handle_send, NULL,
     GNUNET_MESSAGE_TYPE_TRANSPORT_SEND, 0},
    {&clients_handle_request_connect, NULL,
     GNUNET_MESSAGE_TYPE_TRANSPORT_REQUEST_CONNECT,
     sizeof (struct TransportRequestConnectMessage)},
    {&clients_handle_address_lookup, NULL,
     GNUNET_MESSAGE_TYPE_TRANSPORT_ADDRESS_LOOKUP, 0},
    {&clients_handle_peer_address_lookup, NULL,
     GNUNET_MESSAGE_TYPE_TRANSPORT_PEER_ADDRESS_LOOKUP,
     sizeof (struct PeerAddressLookupMessage)},
    {&clients_handle_address_iterate, NULL,
     GNUNET_MESSAGE_TYPE_TRANSPORT_ADDRESS_ITERATE,
     sizeof (struct AddressIterateMessage)},
    {&GST_blacklist_handle_init, NULL,
     GNUNET_MESSAGE_TYPE_TRANSPORT_BLACKLIST_INIT,
     sizeof (struct GNUNET_MessageHeader)},
    {&GST_blacklist_handle_reply, NULL,
     GNUNET_MESSAGE_TYPE_TRANSPORT_BLACKLIST_REPLY,
     sizeof (struct BlacklistMessage)},
    {NULL, NULL, 0, 0}
  };
  GNUNET_SERVER_add_handlers (server, handlers);
  GNUNET_SERVER_disconnect_notify (server, &client_disconnect_notification,
                                   NULL);
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
 * Broadcast the given message to all of our clients.
 *
 * @param msg message to broadcast
 * @param may_drop GNUNET_YES if the message can be dropped / is payload
 */
void
GST_clients_broadcast (const struct GNUNET_MessageHeader *msg, int may_drop)
{
  struct TransportClient *tc;

  for (tc = clients_head; tc != NULL; tc = tc->next)
  {
    if ( (GNUNET_YES == may_drop) &&
	 (GNUNET_YES != tc->send_payload) )
      continue; /* skip, this client does not care about payload */
    unicast (tc, msg, may_drop);
  }
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
                     const struct GNUNET_MessageHeader *msg, int may_drop)
{
  struct TransportClient *tc;

  tc = lookup_client (client);
  if (NULL == tc)
    return;                     /* client got disconnected in the meantime, drop message */
  unicast (tc, msg, may_drop);
}


/* end of file gnunet-service-transport_clients.c */
