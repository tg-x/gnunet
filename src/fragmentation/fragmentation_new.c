/*
     This file is part of GNUnet
     (C) 2009, 2011 Christian Grothoff (and other contributing authors)

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
 * @file src/fragmentation/fragmentation_new.c
 * @brief library to help fragment messages
 * @author Christian Grothoff
 */

#include "platform.h"
#include "gnunet_fragmentation_lib.h"
#include "fragmentation.h"

/**
 * Fragmentation context.
 */
struct GNUNET_FRAGMENT_Context
{
  /**
   * Statistics to use.
   */
  struct GNUNET_STATISTICS_Handle *stats;

  /**
   * Tracker for flow control.
   */
  struct GNUNET_BANDWIDTH_Tracker *tracker;

  /**
   * Current expected delay for ACKs.
   */
  struct GNUNET_TIME_Relative delay;

  /**
   * Message to fragment (allocated at the end of this struct).
   */
  const struct GNUNET_MessageHeader *msg;

  /**
   * Function to call for transmissions.
   */
  GNUNET_FRAGMENT_MessageProcessor proc;

  /**
   * Closure for 'proc'.
   */
  void *proc_cls;

  /**
   * Bitfield, set to 1 for each unacknowledged fragment.
   */
  uint64_t acks;

  /**
   * Task performing work for the fragmenter.
   */
  GNUNET_SCHEDULER_TaskIdentifier task;

  /**
   * Target fragment size.
   */
  uint16_t mtu;
  
};


/**
 * Transmit the next fragment to the other peer.
 *
 * @param cls the 'struct GNUNET_FRAGMENT_Context'
 * @param tc scheduler context
 */
static void
transmit_next (void *cls,
	       const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct GNUNET_FRAGMENT_Context *fc = cls;

  fc->task = GNUNET_SCHEDULER_NO_TASK;
}


/**
 * Create a fragmentation context for the given message.
 * Fragments the message into fragments of size "mtu" or
 * less.  Calls 'proc' on each un-acknowledged fragment,
 * using both the expected 'delay' between messages and
 * acknowledgements and the given 'tracker' to guide the
 * frequency of calls to 'proc'.
 *
 * @param stats statistics context
 * @param mtu the maximum message size for each fragment
 * @param tracker bandwidth tracker to use for flow control (can be NULL)
 * @param delay expected delay between fragment transmission
 *              and ACK based on previous messages
 * @param msg the message to fragment
 * @param proc function to call for each fragment to transmit
 * @param proc_cls closure for proc
 * @return the fragmentation context
 */
struct GNUNET_FRAGMENT_Context *
GNUNET_FRAGMENT_context_create (struct GNUNET_STATISTICS_Handle *stats,
				uint16_t mtu,
				struct GNUNET_BANDWIDTH_Tracker *tracker,
				struct GNUNET_TIME_Relative delay,
				const struct GNUNET_MessageHeader *msg,
				GNUNET_FRAGMENT_MessageProcessor proc,
				void *proc_cls)
{
  struct GNUNET_FRAGMENT_Context *fc;
  size_t size;
  uint64_t bits;
  
  GNUNET_assert (mtu >= 1024 + sizeof (struct FragmentHeader));
  size = ntohs (msg->size);
  GNUNET_assert (size > mtu);
  fc = GNUNET_malloc (sizeof (struct GNUNET_FRAGMENT_Context) + size);
  fc->stats = stats;
  fc->mtu = mtu;
  fc->tracker = tracker;
  fc->delay = delay;
  fc->msg = (const struct GNUNET_MessageHeader*)&fc[1];
  fc->proc = proc;
  fc->proc_cls = proc_cls;
  memcpy (&fc[1], msg, size);
  bits = (size + mtu - 1) / (mtu - sizeof (struct FragmentHeader));
  GNUNET_assert (bits <= 64);
  if (bits == 64)
    fc->acks = UINT64_MAX;      /* set all 64 bit */
  else
    fc->acks = (1 << bits) - 1; /* set lowest 'bits' bit */
  fc->task = GNUNET_SCHEDULER_add_delayed (GNUNET_BANDWIDTH_tracker_get_delay (tracker, mtu),
					   &transmit_next,
					   fc);
  return fc;
}


/**
 * Process an acknowledgement message we got from the other
 * side (to control re-transmits).
 *
 * @param fc fragmentation context
 * @param msg acknowledgement message we received
 * @return GNUNET_OK if this ack completes the work of the 'fc'
 *                   (all fragments have been received);
 *         GNUNET_NO if more messages are pending
 *         GNUNET_SYSERR if this ack is not valid for this fc
 */
int 
GNUNET_FRAGMENT_process_ack (struct GNUNET_FRAGMENT_Context *fc,
			     const struct GNUNET_MessageHeader *msg)
{
  return GNUNET_SYSERR;
}


/**
 * Destroy the given fragmentation context (stop calling 'proc', free
 * resources).
 *
 * @param fc fragmentation context
 * @return average delay between transmission and ACK for the
 *         last message, FOREVER if the message was not fully transmitted
 */
struct GNUNET_TIME_Relative
GNUNET_FRAGMENT_context_destroy (struct GNUNET_FRAGMENT_Context *fc)
{
  struct GNUNET_TIME_Relative ret;

  if (fc->task != GNUNET_SCHEDULER_NO_TASK)
    GNUNET_SCHEDULER_cancel (fc->task);
  ret = fc->delay;
  GNUNET_free (fc);
  return ret;
}

/* end of fragmentation_new.c */

