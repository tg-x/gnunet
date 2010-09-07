/*
     This file is part of GNUnet
     (C) 2006 Christian Grothoff (and other contributing authors)

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
 * @file src/dht/dhtlog.h
 *
 * @brief dhtlog is a service that implements logging of dht operations
 * for testing
 * @author Nathan Evans
 */

#ifndef GNUNET_DHTLOG_SERVICE_H
#define GNUNET_DHTLOG_SERVICE_H

#include "gnunet_util_lib.h"

#ifdef __cplusplus
extern "C"
{
#if 0                           /* keep Emacsens' auto-indent happy */
}
#endif
#endif

typedef enum
{
  /**
   * Type for a DHT GET message
   */
  DHTLOG_GET = 1,

  /**
   * Type for a DHT PUT message
   */
  DHTLOG_PUT = 2,

  /**
   * Type for a DHT FIND PEER message
   */
  DHTLOG_FIND_PEER = 3,

  /**
   * Type for a DHT RESULT message
   */
  DHTLOG_RESULT = 4,

  /**
   * Generic DHT ROUTE message
   */
  DHTLOG_ROUTE = 5,

} DHTLOG_MESSAGE_TYPES;

struct GNUNET_DHTLOG_Handle
{

  /*
   * Inserts the specified query into the dhttests.queries table
   *
   * @param sqlqueruid inserted query uid
   * @param queryid dht query id
   * @param type type of the query
   * @param hops number of hops query traveled
   * @param succeeded whether or not query was successful
   * @param node the node the query hit
   * @param key the key of the query
   *
   * @return GNUNET_OK on success, GNUNET_SYSERR on failure.
   */
  int (*insert_query) (unsigned long long *sqlqueryuid,
                       unsigned long long queryid, DHTLOG_MESSAGE_TYPES type,
                       unsigned int hops,
                       int succeeded,
                       const struct GNUNET_PeerIdentity * node,
                       const GNUNET_HashCode * key);

  /*
   * Inserts the specified trial into the dhttests.trials table
   *
   * @param trialuid return the trialuid of the newly inserted trial
   * @param other_identifier identifier for the trial from another source
   *        (for joining later)
   * @param num_nodes how many nodes are in the trial
   * @param topology integer representing topology for this trial
   * @param blacklist_topology integer representing blacklist topology for this trial
   * @param connect_topology integer representing connect topology for this trial
   * @param connect_topology_option integer representing connect topology option
   * @param connect_topology_option_modifier float to modify connect option
   * @param topology_percentage percentage modifier for certain topologies
   * @param topology_probability probability modifier for certain topologies
   * @param puts number of puts to perform
   * @param gets number of gets to perform
   * @param concurrent number of concurrent requests
   * @param settle_time time to wait between creating topology and starting testing
   * @param num_rounds number of times to repeat the trial
   * @param malicious_getters number of malicious GET peers in the trial
   * @param malicious_putters number of malicious PUT peers in the trial
   * @param malicious_droppers number of malicious DROP peers in the trial
   * @param malicious_get_frequency how often malicious gets are sent
   * @param malicious_put_frequency how often malicious puts are sent
   * @param stop_closest stop forwarding PUTs if closest node found
   * @param stop_found stop forwarding GETs if data found
   * @param strict_kademlia test used kademlia routing algorithm
   * @param gets_succeeded how many gets did the test driver report success on
   * @param message string to put into DB for this trial
   *
   * @return GNUNET_OK on success, GNUNET_SYSERR on failure
   */
  int (*insert_trial) (unsigned long long *trialuid, unsigned int other_identifier, unsigned int num_nodes, unsigned int topology,
                       unsigned int blacklist_topology, unsigned int connect_topology,
                       unsigned int connect_topology_option, float connect_topology_option_modifier,
                       float topology_percentage, float topology_probability,
                       unsigned int puts, unsigned int gets, unsigned int concurrent, unsigned int settle_time,
                       unsigned int num_rounds, unsigned int malicious_getters, unsigned int malicious_putters,
                       unsigned int malicious_droppers, unsigned int malicious_get_frequency,
                       unsigned int malicious_put_frequency, unsigned int stop_closest, unsigned int stop_found,
                       unsigned int strict_kademlia, unsigned int gets_succeeded,
                       char *message);

  /*
   * Inserts the specified stats into the dhttests.node_statistics table
   *
   * @param peer the peer inserting the statistic
   * @param route_requests route requests seen
   * @param route_forwards route requests forwarded
   * @param result_requests route result requests seen
   * @param client_requests client requests initiated
   * @param result_forwards route results forwarded
   * @param gets get requests handled
   * @param puts put requests handle
   * @param data_inserts data inserted at this node
   * @param find_peer_requests find peer requests seen
   * @param find_peers_started find peer requests initiated at this node
   * @param gets_started get requests initiated at this node
   * @param puts_started put requests initiated at this node
   * @param find_peer_responses_received find peer responses received locally
   * @param get_responses_received get responses received locally
   * @param find_peer_responses_sent find peer responses sent from this node
   * @param get_responses_sent get responses sent from this node
   *
   * @return GNUNET_OK on success, GNUNET_SYSERR on failure
   */
  int (*insert_stat)
     (const struct GNUNET_PeerIdentity *peer, unsigned int route_requests,
      unsigned int route_forwards, unsigned int result_requests,
      unsigned int client_requests, unsigned int result_forwards,
      unsigned int gets, unsigned int puts,
      unsigned int data_inserts, unsigned int find_peer_requests,
      unsigned int find_peers_started, unsigned int gets_started,
      unsigned int puts_started, unsigned int find_peer_responses_received,
      unsigned int get_responses_received, unsigned int find_peer_responses_sent,
      unsigned int get_responses_sent);

  /*
   * Update dhttests.trials table with current server time as end time
   *
   * @param trialuid trial to update
   * @param gets_succeeded how many gets did the trial report successful
   *
   * @return GNUNET_OK on success, GNUNET_SYSERR on failure.
   */
  int (*update_trial) (unsigned long long trialuid,
                       unsigned int gets_succeeded);

  /*
   * Update dhttests.nodes table setting the identified
   * node as a malicious dropper.
   *
   * @param peer the peer that was set to be malicious
   *
   * @return GNUNET_OK on success, GNUNET_SYSERR on failure.
   */
  int (*set_malicious) (struct GNUNET_PeerIdentity *peer);

  /*
   * Records the current topology (number of connections, time, trial)
   *
   * @param num_connections how many connections are in the topology
   *
   * @return GNUNET_OK on success, GNUNET_SYSERR on failure
   */
  int (*insert_topology) (int num_connections);

  /*
   * Records a connection between two peers in the current topology
   *
   * @param first one side of the connection
   * @param second other side of the connection
   *
   * @return GNUNET_OK on success, GNUNET_SYSERR on failure
   */
  int (*insert_extended_topology) (const struct GNUNET_PeerIdentity *first, const struct GNUNET_PeerIdentity *second);

  /*
   * Inserts the specified stats into the dhttests.generic_stats table
   *
   * @param peer the peer inserting the statistic
   * @param name the name of the statistic
   * @param section the section of the statistic
   * @param value the value of the statistic
   *
   * @return GNUNET_OK on success, GNUNET_SYSERR on failure
   */
  int
  (*add_generic_stat) (const struct GNUNET_PeerIdentity *peer,
                       const char *name,
                       const char *section, uint64_t value);

  /*
   * Update dhttests.trials table with total connections information
   *
   * @param trialuid the trialuid to update
   * @param totalConnections the number of connections
   *
   * @return GNUNET_OK on success, GNUNET_SYSERR on failure.
   */
  int (*update_connections) (unsigned long long trialuid,
                             unsigned int totalConnections);

  /*
   * Update dhttests.trials table with total connections information
   *
   * @param connections the number of connections
   *
   * @return GNUNET_OK on success, GNUNET_SYSERR on failure.
   */
  int (*update_topology) (unsigned int connections);

  /*
   * Inserts the specified route information into the dhttests.routes table
   *
   * @param sqlqueruid inserted query uid
   * @param queryid dht query id
   * @param type type of the query
   * @param hops number of hops query traveled
   * @param succeeded whether or not query was successful
   * @param node the node the query hit
   * @param key the key of the query
   * @param from_node the node that sent the message to node
   * @param to_node next node to forward message to
   *
   * @return GNUNET_OK on success, GNUNET_SYSERR on failure.
   */
  int (*insert_route) (unsigned long long *sqlqueryuid,
                       unsigned long long queryid,
                       unsigned int type,
                       unsigned int hops,
                       int succeeded,
                       const struct GNUNET_PeerIdentity * node,
                       const GNUNET_HashCode * key,
                       const struct GNUNET_PeerIdentity * from_node,
                       const struct GNUNET_PeerIdentity * to_node);

  /*
   * Inserts the specified node into the dhttests.nodes table
   *
   * @param nodeuid the inserted node uid
   * @param node the node to insert
   *
   * @return GNUNET_OK on success, GNUNET_SYSERR on failure
   */
  int (*insert_node) (unsigned long long *nodeuid,
                      struct GNUNET_PeerIdentity * node);

  /*
   * Inserts the specified dhtkey into the dhttests.dhtkeys table,
   * stores return value of dhttests.dhtkeys.dhtkeyuid into dhtkeyuid
   *
   * @param dhtkeyuid return value
   * @param dhtkey hashcode of key to insert
   *
   * @return GNUNET_OK on success, GNUNET_SYSERR on failure
   */
  int (*insert_dhtkey) (unsigned long long *dhtkeyuid,
                        const GNUNET_HashCode * dhtkey);

};

struct GNUNET_DHTLOG_Plugin
{
  const struct GNUNET_CONFIGURATION_Handle *cfg;

  struct GNUNET_DHTLOG_Handle *dhtlog_api;
};

/**
 * Connect to mysql server using the DHT log plugin.
 *
 * @param c a configuration to use
 */
struct GNUNET_DHTLOG_Handle *
GNUNET_DHTLOG_connect (const struct GNUNET_CONFIGURATION_Handle *c);

/**
 * Shutdown the module.
 */
void
GNUNET_DHTLOG_disconnect (struct GNUNET_DHTLOG_Handle *api);


#if 0                           /* keep Emacsens' auto-indent happy */
{
#endif
#ifdef __cplusplus
}
#endif

/* end of dhtlog.h */
#endif
