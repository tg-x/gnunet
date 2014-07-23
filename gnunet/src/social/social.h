/*
 * This file is part of GNUnet
 * (C) 2013 Christian Grothoff (and other contributing authors)
 *
 * GNUnet is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 3, or (at your
 * option) any later version.
 *
 * GNUnet is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNUnet; see the file COPYING.  If not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * @file social/social.h
 * @brief Common type definitions for the Social service and API.
 * @author Gabor X Toth
 */

#ifndef SOCIAL_H      
#define SOCIAL_H

#include "platform.h"
#include "gnunet_social_service.h"

enum MessageState
{
  MSG_STATE_START    = 0,
  MSG_STATE_HEADER   = 1,
  MSG_STATE_METHOD   = 2,
  MSG_STATE_MODIFIER = 3,
  MSG_STATE_MOD_CONT = 4,
  MSG_STATE_DATA     = 5,
  MSG_STATE_END      = 6,
  MSG_STATE_CANCEL   = 7,
  MSG_STATE_ERROR    = 8,
};


GNUNET_NETWORK_STRUCT_BEGIN

/**** library -> service ****/


struct HostEnterRequest
{
  /**
   * Type: GNUNET_MESSAGE_TYPE_SOCIAL_HOST_ENTER
   */
  struct GNUNET_MessageHeader header;

  uint32_t policy GNUNET_PACKED;

  struct GNUNET_CRYPTO_EcdsaPrivateKey host_key;

  struct GNUNET_CRYPTO_EddsaPrivateKey place_key;
};


struct GuestEnterRequest
{
  /**
   * Type: GNUNET_MESSAGE_TYPE_SOCIAL_GUEST_ENTER_ADDR
   */
  struct GNUNET_MessageHeader header;

  uint32_t relay_count GNUNET_PACKED;

  struct GNUNET_CRYPTO_EcdsaPrivateKey guest_key;

  struct GNUNET_CRYPTO_EddsaPublicKey place_key;

  struct GNUNET_PeerIdentity origin;

  /* Followed by struct GNUNET_PeerIdentity relays[relay_count] */

  /* Followed by struct GNUNET_MessageHeader join_msg */
};


/**** service -> library ****/


struct CountersResult
{
  /**
   * Type: GNUNET_MESSAGE_TYPE_PSYC_RESULT_COUNTERS
   */
  struct GNUNET_MessageHeader header;

  /**
   * Status code for the operation.
   */
  int32_t result_code GNUNET_PACKED;

  /**
   * Last message ID sent to the channel.
   */
  uint64_t max_message_id;
};


#if REMOVE
struct NymEnterRequest
{
  /**
   * Type: GNUNET_MESSAGE_TYPE_SOCIAL_NYM_ENTER
   */
  struct GNUNET_MessageHeader header;
  /**
   * Public key of the joining slave.
   */
  struct GNUNET_CRYPTO_EcdsaPublicKey nym_key;

  /* Followed by struct GNUNET_MessageHeader join_request */
};
#endif


GNUNET_NETWORK_STRUCT_END

#endif
