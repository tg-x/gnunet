/*
     This file is part of GNUnet.
     (C) 2009 Christian Grothoff (and other contributing authors)

     GNUnet is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published
     by the Free Software Foundation; either version 2, or (at your
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
 * @file dv/gnunet-service-dv.c
 * @brief the distance vector service, primarily handles gossip of nearby
 * peers and sending/receiving DV messages from core and decapsulating
 * them
 *
 * @author Christian Grothoff
 * @author Nathan Evans
 *
 */
#include "platform.h"
#include "gnunet_client_lib.h"
#include "gnunet_getopt_lib.h"
#include "gnunet_os_lib.h"
#include "gnunet_protocols.h"
#include "gnunet_service_lib.h"
#include "gnunet_core_service.h"
#include "gnunet_signal_lib.h"
#include "gnunet_util_lib.h"
#include "dv.h"

/**
 * DV Service Context stuff goes here...
 */

/**
 * Handle to the core service api.
 */
static struct GNUNET_CORE_Handle *coreAPI;

/**
 * The identity of our peer.
 */
const struct GNUNET_PeerIdentity *my_identity;

/**
 * The configuration for this service.
 */
const struct GNUNET_CONFIGURATION_Handle *cfg;

/**
 * The scheduler for this service.
 */
static struct GNUNET_SCHEDULER_Handle *sched;

/**
 * How often do we check about sending out more peer information (if
 * we are connected to no peers previously).
 */
#define GNUNET_DV_DEFAULT_SEND_INTERVAL (500 * GNUNET_CRON_MILLISECONDS)

/**
 * How long do we wait at most between sending out information?
 */
#define GNUNET_DV_MAX_SEND_INTERVAL (5000 * GNUNET_CRON_MILLISECONDS)

/**
 * How long can we have not heard from a peer and
 * still have it in our tables?
 */
#define GNUNET_DV_PEER_EXPIRATION_TIME (3000 * GNUNET_CRON_SECONDS)

/**
 * Priority for gossip.
 */
#define GNUNET_DV_DHT_GOSSIP_PRIORITY (GNUNET_EXTREME_PRIORITY / 10)

/**
 * How often should we check if expiration time has elapsed for
 * some peer?
 */
#define GNUNET_DV_MAINTAIN_FREQUENCY (5 * GNUNET_CRON_SECONDS)

/**
 * How long to allow a message to be delayed?
 */
#define DV_DELAY (5000 * GNUNET_CRON_MILLISECONDS)

/**
 * Priority to use for DV data messages.
 */
#define DV_PRIORITY 0

/**
 * The client, should be the DV plugin connected to us.  Hopefully
 * this client will never change, although if the plugin dies
 * and returns for some reason it may happen.
 */
static struct GNUNET_SERVER_Client * client_handle;

GNUNET_SCHEDULER_TaskIdentifier cleanup_task;

/**
 * Struct where neighbor information is stored.
 */
struct DistantNeighbor *referees;

/**
 * Struct to hold information for updating existing neighbors
 */
struct NeighborUpdateInfo
{
  /**
   * Cost
   */
  unsigned int cost;

  /**
   * The existing neighbor
   */
  struct DistantNeighbor *neighbor;

  /**
   * The referrer of the possibly existing peer
   */
  struct DirectNeighbor *referrer;

  /**
   * The time we heard about this peer
   */
  struct GNUNET_TIME_Absolute now;
};

/**
 * Struct where actual neighbor information is stored,
 * referenced by min_heap and max_heap.  Freeing dealt
 * with when items removed from hashmap.
 */
struct DirectNeighbor
{
  /**
   * Identity of neighbor.
   */
  struct GNUNET_PeerIdentity identity;

  /**
   * Head of DLL of nodes that this direct neighbor referred to us.
   */
  struct DistantNeighbor *referee_head;

  /**
   * Tail of DLL of nodes that this direct neighbor referred to us.
   */
  struct DistantNeighbor *referee_tail;

  /**
   * Is this one of the direct neighbors that we are "hiding"
   * from DV?
   */
  int hidden;
};


/**
 * Struct where actual neighbor information is stored,
 * referenced by min_heap and max_heap.  Freeing dealt
 * with when items removed from hashmap.
 */
struct DistantNeighbor
{
  /**
   * We keep distant neighbor's of the same referrer in a DLL.
   */
  struct DistantNeighbor *next;

  /**
   * We keep distant neighbor's of the same referrer in a DLL.
   */
  struct DistantNeighbor *prev;

  /**
   * Node in min heap
   */
  struct GNUNET_CONTAINER_HeapNode *min_loc;

  /**
   * Node in max heap
   */
  struct GNUNET_CONTAINER_HeapNode *max_loc;

  /**
   * Identity of referrer (next hop towards 'neighbor').
   */
  struct DirectNeighbor *referrer;

  /**
   * Identity of neighbor.
   */
  struct GNUNET_PeerIdentity identity;

  /**
   * Last time we received routing information from this peer
   */
  struct GNUNET_TIME_Absolute last_activity;

  /**
   * Cost to neighbor, used for actual distance vector computations
   */
  unsigned int cost;

  /**
   * Random identifier *we* use for this peer, to be used as shortcut
   * instead of sending full peer id for each message
   */
  unsigned int our_id;

  /**
   * Random identifier the *referrer* uses for this peer.
   */
  unsigned int referrer_id;

  /**
   * Is this one of the direct neighbors that we are "hiding"
   * from DV?
   */
  int hidden;
};


/**
 * Global construct
 */
struct GNUNET_DV_Context
{
  /**
   * Map of PeerIdentifiers to 'struct GNUNET_dv_neighbor*'s for all
   * directly connected peers.
   */
  struct GNUNET_CONTAINER_MultiHashMap *direct_neighbors;

  /**
   * Map of PeerIdentifiers to 'struct GNUNET_dv_neighbor*'s for
   * peers connected via DV (extended neighborhood).  Does ALSO
   * include any peers that are in 'direct_neighbors'; for those
   * peers, the cost will be zero and the referrer all zeros.
   */
  struct GNUNET_CONTAINER_MultiHashMap *extended_neighbors;

  /**
   * We use the min heap (min refers to cost) to prefer
   * gossipping about peers with small costs.
   */
  struct GNUNET_CONTAINER_Heap *neighbor_min_heap;

  /**
   * We use the max heap (max refers to cost) for general
   * iterations over all peers and to remove the most costly
   * connection if we have too many.
   */
  struct GNUNET_CONTAINER_Heap *neighbor_max_heap;

  unsigned long long fisheye_depth;

  unsigned long long max_table_size;

  unsigned int send_interval;

  unsigned int neighbor_id_loc;

  int closing;
};

static char shortID[5];

static struct GNUNET_DV_Context ctx;

struct FindDestinationContext
{
  unsigned int tid;
  struct DistantNeighbor *dest;
};


/**
 * We've been given a target ID based on the random numbers that
 * we assigned to our DV-neighborhood.  Find the entry for the
 * respective neighbor.
 */
static int
find_destination (void *cls,
                  struct GNUNET_CONTAINER_HeapNode *node,
                  void *element, GNUNET_CONTAINER_HeapCostType cost)
{
  struct FindDestinationContext *fdc = cls;
  struct DistantNeighbor *dn = element;

  if (fdc->tid != dn->our_id)
    return GNUNET_YES;
  fdc->dest = dn;
  return GNUNET_NO;
}

/**
 * Function called to notify a client about the socket
 * begin ready to queue more data.  "buf" will be
 * NULL and "size" zero if the socket was closed for
 * writing in the meantime.
 *
 * @param cls closure
 * @param size number of bytes available in buf
 * @param buf where the callee should write the message
 * @return number of bytes written to buf
 */
size_t transmit_to_plugin (void *cls,
               size_t size, void *buf)
{
  struct GNUNET_DV_MessageReceived *msg = cls;

  if (buf == NULL)
    return 0;

  GNUNET_assert(size >= ntohs(msg->header.size));

  memcpy(buf, msg, size);
  GNUNET_free(msg);
  return size;
}


void send_to_plugin(const struct GNUNET_PeerIdentity * sender, const struct GNUNET_MessageHeader *message, size_t message_size, struct DistantNeighbor *distant_neighbor)
{
  struct GNUNET_DV_MessageReceived *received_msg;
  int size;

  if (ntohs(msg->size) < sizeof(struct GNUNET_DV_MessageReceived))
    return;

  size = sizeof(struct GNUNET_DV_MessageReceived) + message_size + sizeof(struct GNUNET_PeerIdentity);
  received_msg = GNUNET_malloc(size);
  received_msg->header.size = htons(size);
  received_msg->header.type = htons(GNUNET_MESSAGE_TYPE_TRANSPORT_DV_RECEIVE);
  received_msg->sender_address_len = sizeof(struct GNUNET_PeerIdentity);
  received_msg->distance = htonl(distant_neighbor->cost);
  /* Set the sender in this message to be the original sender! */
  memcpy(&received_msg->sender, &distant_neighbor->identity, sizeof(struct GNUNET_PeerIdentity));
  /* Copy the intermediate sender to the end of the message, this is how the transport identifies this peer */
  memcpy(&received_msg[1], sender, sizeof(struct GNUNET_PeerIdentity));

  /* FIXME: Send to the client please */
  GNUNET_SERVER_notify_transmit_ready (client_handle,
                                       size, CLIENT_TRANSMIT_TIMEOUT,
                                       &transmit_to_plugin, &received_msg);

}

/**
 * Core handler for dv data messages.  Whatever this message
 * contains all we really have to do is rip it out of its
 * DV layering and give it to our pal the DV plugin to report
 * in with.
 *
 * @param cls closure
 * @param peer peer which sent the message (immediate sender)
 * @param message the message
 * @param latency the latency of the connection we received the message from
 * @param distance the distance to the immediate peer
 */
static int handle_dv_data_message (void *cls,
                             const struct GNUNET_PeerIdentity * peer,
                             const struct GNUNET_MessageHeader * message,
                             struct GNUNET_TIME_Relative latency,
                             uint32_t distance)
{
#if DEBUG_DV
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "%s: Receives %s message!\n", "dv", "DV DATA");
#endif

  const p2p_dv_MESSAGE_Data *incoming = (const p2p_dv_MESSAGE_Data *) message;
  const struct GNUNET_MessageHeader *packed_message = (const struct GNUNET_MessageHeader *) &incoming[1];
  struct DirectNeighbor *dn;
  struct DistantNeighbor *pos;
  unsigned int sid;             /* Sender id */
  unsigned int tid;             /* Target id */
  struct GNUNET_PeerIdentity original_sender;
  struct GNUNET_PeerIdentity destination;
  struct FindDestinationContext fdc;
  int ret;

  if ((ntohs (incoming->header.size) <
       sizeof (p2p_dv_MESSAGE_Data) + sizeof (struct GNUNET_MessageHeader))
      || (ntohs (incoming->header.size) !=
          (sizeof (p2p_dv_MESSAGE_Data) + ntohs (packed_message->size))))
    {
      return GNUNET_SYSERR;
    }

  dn = GNUNET_CONTAINER_multihashmap_get (ctx.direct_neighbors,
                                  &peer->hashPubKey);
  if (dn == NULL)
    {
      return GNUNET_OK;
    }
  sid = ntohl (incoming->sender);
  pos = dn->referee_head;
  while ((NULL != pos) && (pos->referrer_id != sid))
    pos = pos->next;
  if (pos == NULL)
    {
      /* unknown sender */
      return GNUNET_OK;
    }
  original_sender = pos->identity;
  tid = ntohl (incoming->recipient);
  if (tid == 0)
    {
      /* 0 == us */

      /* FIXME: Will we support wrapped messages being these types? Probably not, they should
       * be encrypted messages that need decrypting and junk like that.
       */
      GNUNET_break_op (ntohs (packed_message->type) != GNUNET_MESSAGE_TYPE_DV_GOSSIP);
      GNUNET_break_op (ntohs (packed_message->type) != GNUNET_MESSAGE_TYPE_DV_DATA);
      if ( (ntohs (packed_message->type) != GNUNET_MESSAGE_TYPE_DV_GOSSIP) &&
          (ntohs (packed_message->type) != GNUNET_MESSAGE_TYPE_DV_DATA) )
      {
        /* FIXME: send the message, wrap it up and return it to the DV plugin */
        /*coreAPI->loopback_send (&original_sender, (const char *) packed_message,
        ntohs (packed_message->size), GNUNET_YES, NULL);*/
        send_to_plugin(peer, packed_message, ntohs(packed_message->size), pos);
      }

      return GNUNET_OK;
    }

  /* FIXME: this is the *only* per-request operation we have in DV
     that is O(n) in relation to the number of connected peers; a
     hash-table lookup could easily solve this (minor performance
     issue) */
  fdc.tid = tid;
  fdc.dest = NULL;
  GNUNET_CONTAINER_heap_iterate (ctx.neighbor_max_heap,
                                 &find_destination, &fdc);
  if (fdc.dest == NULL)
    {
      return GNUNET_OK;
    }
  destination = fdc.dest->identity;

  if (0 == memcmp (&destination, peer, sizeof (struct GNUNET_PeerIdentity)))
    {
      /* FIXME: create stat: routing loop-discard! */
      return GNUNET_OK;
    }

  /* FIXME: Can't send message on, we have to behave.
   * We have to tell core we have a message for the next peer, and let
   * transport do transport selection on how to get this message to 'em */
  /*ret = send_message (&destination,
                      &original_sender,
                      packed_message, DV_PRIORITY, DV_DELAY);*/
  send_to_core(&destination, &original_sender, packed_message, DV_PRIORITY, DV_DELAY);

  return GNUNET_OK;
}

/**
 * Core handler for dv gossip messages.  These will be used
 * by us to create a HELLO message for the newly peer containing
 * which direct peer we can connect through, and what the cost
 * is.  This HELLO will then be scheduled for validation by the
 * transport service so that it can be used by all others.
 *
 * @param cls closure
 * @param peer peer which sent the message (immediate sender)
 * @param message the message
 * @param latency the latency of the connection we received the message from
 * @param distance the distance to the immediate peer
 */
static int handle_dv_gossip_message (void *cls,
                                     const struct GNUNET_PeerIdentity *peer,
                                     const struct GNUNET_MessageHeader *message,
                                     struct GNUNET_TIME_Relative latency,
                                     uint32_t distance)
{
#if DEBUG_DV
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "%s: Receives %s message!\n", "dv", "DV GOSSIP");
#endif

  return 0;
}


/**
 * Service server's handler for message send requests (which come
 * bubbling up to us through the DV plugin).
 *
 * @param cls closure
 * @param client identification of the client
 * @param message the actual message
 */
void send_dv_message (void *cls,
                      struct GNUNET_SERVER_Client * client,
                      const struct GNUNET_MessageHeader * message)
{
#if DEBUG_DV
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "%s: Receives %s message!\n", "dv", "SEND");
#endif
  if (client_handle == NULL)
  {
    client_handle = client;
#if DEBUG_DV
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "%s: Setting initial client handle!\n", "dv");
#endif
  }
  else if (client_handle != client)
  {
    client_handle = client;
    /* What should we do in this case, assert fail or just log the warning? */
    GNUNET_log (GNUNET_ERROR_TYPE_WARNING,
                "%s: Setting client handle (was a different client!)!\n", "dv");
  }

  GNUNET_SERVER_receive_done(client, GNUNET_OK);
}


/**
 * List of handlers for the messages understood by this
 * service.
 *
 * Hmm... will we need to register some handlers with core and
 * some handlers with our server here?  Because core should be
 * getting the incoming DV messages (from whichever lower level
 * transport) and then our server should be getting messages
 * from the dv_plugin, right?
 */
static struct GNUNET_CORE_MessageHandler core_handlers[] = {
  {&handle_dv_data_message, GNUNET_MESSAGE_TYPE_DV_DATA, 0},
  {&handle_dv_gossip_message, GNUNET_MESSAGE_TYPE_DV_GOSSIP, 0},
  {NULL, 0, 0}
};


static struct GNUNET_SERVER_MessageHandler plugin_handlers[] = {
  {&send_dv_message, NULL, GNUNET_MESSAGE_TYPE_TRANSPORT_DV_SEND, 0},
  {NULL, NULL, 0, 0}
};


/**
 * Task run during shutdown.
 *
 * @param cls unused
 * @param tc unused
 */
static void
shutdown_task (void *cls,
               const struct GNUNET_SCHEDULER_TaskContext *tc)
{

  GNUNET_CORE_disconnect (coreAPI);
}

/**
 * To be called on core init/fail.
 */
void core_init (void *cls,
                struct GNUNET_CORE_Handle * server,
                const struct GNUNET_PeerIdentity *identity,
                const struct GNUNET_CRYPTO_RsaPublicKeyBinaryEncoded * publicKey)
{

  if (server == NULL)
    {
      GNUNET_SCHEDULER_cancel(sched, cleanup_task);
      GNUNET_SCHEDULER_add_now(sched, &shutdown_task, NULL);
      return;
    }
#if DEBUG_DV
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "%s: Core connection initialized, I am peer: %s\n", "dv", GNUNET_i2s(identity));
#endif
  my_identity = identity;
  coreAPI = server;
}


/**
 * Iterator over hash map entries.
 *
 * @param cls closure
 * @param key current key code
 * @param value value in the hash map
 * @return GNUNET_YES if we should continue to
 *         iterate,
 *         GNUNET_NO if not.
 */
static int update_matching_neighbors (void *cls,
                                      const GNUNET_HashCode * key,
                                      void *value)
{
  struct NeighborUpdateInfo * update_info = cls;
  struct DirectNeighbor *direct_neighbor = value;

  if (update_info->referrer == direct_neighbor) /* Direct neighbor matches, update it's info and return GNUNET_NO */
  {
    /* same referrer, cost change! */
    GNUNET_CONTAINER_heap_update_cost (ctx.neighbor_max_heap,
                                       update_info->neighbor->max_loc, update_info->cost);
    GNUNET_CONTAINER_heap_update_cost (ctx.neighbor_min_heap,
                                       update_info->neighbor->min_loc, update_info->cost);
    update_info->neighbor->last_activity = update_info->now;
    update_info->neighbor->cost = update_info->cost;
    return GNUNET_NO;
  }

  return GNUNET_YES;
}


/**
 * Free a DistantNeighbor node, including removing it
 * from the referer's list.
 */
static void
distant_neighbor_free (struct DistantNeighbor *referee)
{
  struct DirectNeighbor *referrer;

  referrer = referee->referrer;
  if (referrer != NULL)
    {
      GNUNET_CONTAINER_DLL_remove (referrer->referee_head,
                         referrer->referee_tail, referee);
    }
  GNUNET_CONTAINER_heap_remove_node (ctx.neighbor_max_heap, referee->max_loc);
  GNUNET_CONTAINER_heap_remove_node (ctx.neighbor_min_heap, referee->min_loc);
  GNUNET_CONTAINER_multihashmap_remove_all (ctx.extended_neighbors,
                                    &referee->identity.hashPubKey);
  GNUNET_free (referee);
}


/**
 * Handles when a peer is either added due to being newly connected
 * or having been gossiped about, also called when a cost for a neighbor
 * needs to be updated.
 *
 * @param peer identity of the peer whose info is being added/updated
 * @param peer_id id to use when sending to 'peer'
 * @param referrer if this is a gossiped peer, who did we hear it from?
 * @param cost the cost of communicating with this peer via 'referrer'
 */
static void
addUpdateNeighbor (const struct GNUNET_PeerIdentity * peer,
                   unsigned int referrer_peer_id,
                   struct DirectNeighbor *referrer, unsigned int cost)
{
  struct DistantNeighbor *neighbor;
  struct DistantNeighbor *max;
  struct GNUNET_TIME_Absolute now;
  struct NeighborUpdateInfo *neighbor_update;
  unsigned int our_id;

  now = GNUNET_TIME_absolute_get ();
  our_id = GNUNET_CRYPTO_random_u32 (GNUNET_CRYPTO_QUALITY_WEAK, RAND_MAX - 1) + 1;

  neighbor = GNUNET_CONTAINER_multihashmap_get (ctx.extended_neighbors,
                                                &peer->hashPubKey);
  neighbor_update = GNUNET_malloc(sizeof(struct NeighborUpdateInfo));
  neighbor_update->neighbor = neighbor;
  neighbor_update->cost = cost;
  neighbor_update->now = now;
  neighbor_update->referrer = referrer;

  /* Either we do not know this peer, or we already do but via a different immediate peer */
  if ((neighbor == NULL) ||
      (GNUNET_CONTAINER_multihashmap_get_multiple(ctx.extended_neighbors,
                                                  &peer->hashPubKey,
                                                  &update_matching_neighbors,
                                                  neighbor_update) != GNUNET_SYSERR))
    {
      /* new neighbor! */
      if (cost > ctx.fisheye_depth)
        {
          /* too costly */
          return;
        }
      if (ctx.max_table_size <=
          GNUNET_CONTAINER_multihashmap_size (ctx.extended_neighbors))
        {
          /* remove most expensive entry */
          max = GNUNET_CONTAINER_heap_peek (ctx.neighbor_max_heap);
          if (cost > max->cost)
            {
              /* new entry most expensive, don't create */
              return;
            }
          if (max->cost > 0)
            {
              /* only free if this is not a direct connection;
                 we could theoretically have more direct
                 connections than DV entries allowed total! */
              distant_neighbor_free (max);
            }
        }

      neighbor = GNUNET_malloc (sizeof (struct DistantNeighbor));
      GNUNET_CONTAINER_DLL_insert (referrer->referee_head,
                         referrer->referee_tail, neighbor);
      neighbor->max_loc = GNUNET_CONTAINER_heap_insert (ctx.neighbor_max_heap,
                                                        neighbor, cost);
      neighbor->min_loc = GNUNET_CONTAINER_heap_insert (ctx.neighbor_min_heap,
                                                        neighbor, cost);
      neighbor->referrer = referrer;
      memcpy (&neighbor->identity, peer, sizeof (struct GNUNET_PeerIdentity));
      neighbor->last_activity = now;
      neighbor->cost = cost;
      neighbor->referrer_id = referrer_peer_id;
      neighbor->our_id = our_id;
      neighbor->hidden =
        (cost == 0) ? (GNUNET_CRYPTO_random_u32 (GNUNET_CRYPTO_QUALITY_WEAK, 4) ==
                       0) : GNUNET_NO;
      GNUNET_CONTAINER_multihashmap_put (ctx.extended_neighbors, &peer->hashPubKey,
                                 neighbor,
                                 GNUNET_CONTAINER_MULTIHASHMAPOPTION_MULTIPLE);

      return;
    }

  /* Old logic to remove entry and replace, not needed now as we only want to remove when full
   * or when the referring peer disconnects from us.
   */
  /*
  GNUNET_DLL_remove (neighbor->referrer->referee_head,
                     neighbor->referrer->referee_tail, neighbor);
  neighbor->referrer = referrer;
  GNUNET_DLL_insert (referrer->referee_head,
                     referrer->referee_tail, neighbor);
  GNUNET_CONTAINER_heap_update_cost (ctx.neighbor_max_heap,
                                     neighbor->max_loc, cost);
  GNUNET_CONTAINER_heap_update_cost (ctx.neighbor_min_heap,
                                     neighbor->min_loc, cost);
  neighbor->referrer_id = referrer_peer_id;
  neighbor->last_activity = now;
  neighbor->cost = cost;
  */
}


/**
 * Method called whenever a given peer either connects.
 *
 * @param cls closure
 * @param peer peer identity this notification is about
 * @param latency reported latency of the connection with peer
 * @param distance reported distance (DV) to peer
 */
void handle_core_connect (void *cls,
                          const struct GNUNET_PeerIdentity * peer,
                          struct GNUNET_TIME_Relative latency,
                          uint32_t distance)
{
  struct DirectNeighbor *neighbor;
#if DEBUG_DV
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "%s: Receives core connect message for peer %s distance %d!\n", "dv", GNUNET_i2s(peer), distance);
#endif

  neighbor = GNUNET_malloc (sizeof (struct DirectNeighbor));
  memcpy (&neighbor->identity, peer, sizeof (struct GNUNET_PeerIdentity));
  GNUNET_CONTAINER_multihashmap_put (ctx.direct_neighbors,
                             &peer->hashPubKey,
                             neighbor, GNUNET_CONTAINER_MULTIHASHMAPOPTION_UNIQUE_ONLY);
  addUpdateNeighbor (peer, 0, neighbor, 0);
}

/**
 * Method called whenever a given peer either connects.
 *
 * @param cls closure
 * @param peer peer identity this notification is about
 */
void handle_core_disconnect (void *cls,
                             const struct GNUNET_PeerIdentity * peer)
{
  struct DirectNeighbor *neighbor;
  struct DistantNeighbor *referee;

#if DEBUG_DV
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "%s: Receives core peer disconnect message!\n", "dv");
#endif

  neighbor =
    GNUNET_CONTAINER_multihashmap_get (ctx.direct_neighbors, &peer->hashPubKey);
  if (neighbor == NULL)
    {
      return;
    }
  while (NULL != (referee = neighbor->referee_head))
    distant_neighbor_free (referee);
  GNUNET_assert (neighbor->referee_tail == NULL);
  GNUNET_CONTAINER_multihashmap_remove (ctx.direct_neighbors,
                                &peer->hashPubKey, neighbor);
  GNUNET_free (neighbor);
}


/**
 * Process dv requests.
 *
 * @param cls closure
 * @param scheduler scheduler to use
 * @param server the initialized server
 * @param c configuration to use
 */
static void
run (void *cls,
     struct GNUNET_SCHEDULER_Handle *scheduler,
     struct GNUNET_SERVER_Handle *server,
     const struct GNUNET_CONFIGURATION_Handle *c)
{
  struct GNUNET_TIME_Relative timeout;

  timeout = GNUNET_TIME_relative_multiply(GNUNET_TIME_UNIT_SECONDS, 5);
  sched = scheduler;
  cfg = c;
  GNUNET_SERVER_add_handlers (server, plugin_handlers);
  coreAPI =
  GNUNET_CORE_connect (sched,
                       cfg,
                       timeout,
                       NULL, /* FIXME: anything we want to pass around? */
                       &core_init,
                       NULL, /* Don't care about pre-connects */
                       &handle_core_connect,
                       &handle_core_disconnect,
                       NULL,
                       GNUNET_NO,
                       NULL,
                       GNUNET_NO,
                       core_handlers);

  if (coreAPI == NULL)
    return;
  /* load (server); Huh? */

  /* Scheduled the task to clean up when shutdown is called */

  cleanup_task = GNUNET_SCHEDULER_add_delayed (sched,
                                GNUNET_TIME_UNIT_FOREVER_REL,
                                &shutdown_task,
                                NULL);
}


/**
 * The main function for the dv service.
 *
 * @param argc number of arguments from the command line
 * @param argv command line arguments
 * @return 0 ok, 1 on error
 */
int
main (int argc, char *const *argv)
{
  return (GNUNET_OK ==
          GNUNET_SERVICE_run (argc,
                              argv,
                              "dv",
                              GNUNET_SERVICE_OPTION_NONE,
                              &run, NULL)) ? 0 : 1;
}
