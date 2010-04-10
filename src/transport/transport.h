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
 * @file transport/transport.h
 * @brief common internal definitions for transport service
 * @author Christian Grothoff
 */
#ifndef TRANSPORT_H
#define TRANSPORT_H

#include "gnunet_crypto_lib.h"
#include "gnunet_time_lib.h"
#include "gnunet_transport_service.h"

#define DEBUG_TRANSPORT GNUNET_YES
#define DEBUG_TRANSPORT_TIMEOUT GNUNET_YES
#define DEBUG_TRANSPORT_DISCONNECT GNUNET_YES

/**
 * For how long do we allow unused bandwidth
 * from the past to carry over into the future? (in seconds)
 */
#define MAX_BANDWIDTH_CARRY_S 5

/**
 * How often do we (at most) do a full quota
 * recalculation? (in ms)
 */
#define MIN_QUOTA_REFRESH_TIME 2000

/**
 * Message from the transport service to the library
 * informing about neighbors.
 */
struct ConnectInfoMessage
{

  /**
   * Type will be GNUNET_MESSAGE_TYPE_TRANSPORT_CONNECT
   */
  struct GNUNET_MessageHeader header;

  /**
   * Transport distance metric (i.e. hops for DV)
   */
  uint32_t distance;

  /**
   * Latency estimate.
   */
  struct GNUNET_TIME_RelativeNBO latency;

  /**
   * Identity of the new neighbour.
   */
  struct GNUNET_PeerIdentity id;

};


/**
 * Message from the transport service to the library
 * informing about disconnects.
 */
struct DisconnectInfoMessage
{

  /**
   * Type will be GNUNET_MESSAGE_TYPE_TRANSPORT_DISCONNECT
   */
  struct GNUNET_MessageHeader header;

  /**
   * Reserved, always zero.
   */
  uint32_t reserved GNUNET_PACKED;

  /**
   * Who got disconnected?
   */
  struct GNUNET_PeerIdentity peer;

};


/**
 * Message used to set a particular bandwidth quota.  Send TO the
 * service to set an incoming quota, send FROM the service to update
 * an outgoing quota.
 */
struct QuotaSetMessage
{

  /**
   * Type will be GNUNET_MESSAGE_TYPE_TRANSPORT_NEIGHBOUR_INFO
   */
  struct GNUNET_MessageHeader header;

  /**
   * Quota.
   */
  struct GNUNET_BANDWIDTH_Value32NBO quota;

  /**
   * About which peer are we talking here?
   */
  struct GNUNET_PeerIdentity peer;

};


/**
 * Message used to notify the transport API about a message
 * received from the network.  The actual message follows.
 */
struct InboundMessage
{

  /**
   * Type will be GNUNET_MESSAGE_TYPE_TRANSPORT_RECV
   */
  struct GNUNET_MessageHeader header;

  /**
   * Always zero.
   */
  uint32_t reserved GNUNET_PACKED;

  /**
   * Latency estimate.
   */
  struct GNUNET_TIME_RelativeNBO latency;

  /**
   * Which peer sent the message?
   */
  struct GNUNET_PeerIdentity peer;

  /**
   * Distance metric.
   */
  uint32_t distance;

};


/**
 * Message used to notify the transport API that it can
 * send another message to the transport service.
 */
struct SendOkMessage
{

  /**
   * Type will be GNUNET_MESSAGE_TYPE_TRANSPORT_SEND_OK
   */
  struct GNUNET_MessageHeader header;

  /**
   * GNUNET_OK if the transmission succeeded,
   * GNUNET_SYSERR if it failed (i.e. network disconnect);
   * in either case, it is now OK for this client to
   * send us another message for the given peer.
   */
  uint32_t success GNUNET_PACKED;

  /**
   * Latency estimate.
   */
  struct GNUNET_TIME_RelativeNBO latency;

  /**
   * Which peer can send more now?
   */
  struct GNUNET_PeerIdentity peer;

};


/**
 * Message used to notify the transport service about a message
 * to be transmitted to another peer.  The actual message follows.
 */
struct OutboundMessage
{

  /**
   * Type will be GNUNET_MESSAGE_TYPE_TRANSPORT_SEND
   */
  struct GNUNET_MessageHeader header;

  /**
   * Message priority.
   */
  uint32_t priority GNUNET_PACKED;

  /**
   * Allowed delay.
   */
  struct GNUNET_TIME_RelativeNBO timeout;

  /**
   * Which peer should receive the message?
   */
  struct GNUNET_PeerIdentity peer;

};


/**
 * Message from the library to the transport service
 * asking for converting a transport address to a
 * human-readable UTF-8 string.
 */
struct AddressLookupMessage
{

  /**
   * Type will be GNUNET_MESSAGE_TYPE_TRANSPORT_ADDRESS_LOOKUP
   */
  struct GNUNET_MessageHeader header;

  /**
   * Should the conversion use numeric IP addresses (otherwise
   * a reverse DNS lookup is OK -- if applicable).
   */
  int32_t numeric_only GNUNET_PACKED;

  /**
   * timeout to give up.
   */
  struct GNUNET_TIME_AbsoluteNBO timeout;

  /**
   * Length of the (binary) address in bytes, in big-endian.
   */
  uint32_t addrlen GNUNET_PACKED;

  /* followed by 'addrlen' bytes of the actual address, then
     followed by the 0-terminated name of the transport */
};



/**
 * Change in blacklisting (either request or notification,
 * depending on which direction it is going).
 */
struct BlacklistMessage
{

  /**
   * Type will be GNUNET_MESSAGE_TYPE_TRANSPORT_BLACKLIST
   */
  struct GNUNET_MessageHeader header;

  /**
   * Reserved (for alignment).
   */
  uint32_t reserved GNUNET_PACKED;

  /**
   * Which peer is being blacklisted (or has seen its
   * blacklisting expire)?
   */
  struct GNUNET_PeerIdentity peer;

  /**
   * Until what time is this peer blacklisted (zero for
   * no longer blacklisted).
   */
  struct GNUNET_TIME_AbsoluteNBO until;

};


/* end of transport.h */
#endif
