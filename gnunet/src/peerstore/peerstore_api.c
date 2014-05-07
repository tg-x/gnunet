/*
     This file is part of GNUnet.
     (C) 

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
 * @file peerstore/peerstore_api.c
 * @brief API for peerstore
 * @author Omar Tarabai
 */
#include "platform.h"
#include "gnunet_util_lib.h"
#include "peerstore.h"

#define LOG(kind,...) GNUNET_log_from (kind, "peerstore-api",__VA_ARGS__)

/******************************************************************************/
/************************      DATA STRUCTURES     ****************************/
/******************************************************************************/

/**
 * Handle to the PEERSTORE service.
 */
struct GNUNET_PEERSTORE_Handle
{

  /**
   * Our configuration.
   */
    const struct GNUNET_CONFIGURATION_Handle *cfg;

  /**
   * Connection to the service.
   */
  struct GNUNET_CLIENT_Connection *client;

  /**
   * Head of transmission queue.
   */
  struct GNUNET_PEERSTORE_AddContext *ac_head;

  /**
   * Tail of transmission queue.
   */
  struct GNUNET_PEERSTORE_AddContext *ac_tail;

  /**
   * Handle for the current transmission request, or NULL if none is pending.
   */
  struct GNUNET_CLIENT_TransmitHandle *th;

  /**
   * ID for a reconnect task.
   */
  GNUNET_SCHEDULER_TaskIdentifier r_task;

  /**
   * Are we now receiving?
   */
  int in_receive;

};

/**
 * Entry in the transmission queue to PEERSTORE service.
 *
 */
struct GNUNET_PEERSTORE_AddContext
{
  /**
   * This is a linked list.
   */
  struct GNUNET_PEERSTORE_AddContext *next;

  /**
   * This is a linked list.
   */
  struct GNUNET_PEERSTORE_AddContext *prev;

  /**
   * Handle to the PEERSTORE service.
   */
  struct GNUNET_PEERSTORE_Handle *h;

  /**
   * Function to call after request has been transmitted, or NULL.
   */
  GNUNET_PEERSTORE_Continuation cont;

  /**
   * Closure for 'cont'.
   */
  void *cont_cls;

  /**
   * Number of bytes of the request message (follows after this struct).
   */
  size_t size;

};

/******************************************************************************/
/***********************         DECLARATIONS         *************************/
/******************************************************************************/

/**
 * Close the existing connection to PEERSTORE and reconnect.
 *
 * @param h handle to the service
 */
static void
reconnect (struct GNUNET_PEERSTORE_Handle *h);

/**
 * Check if we have a request pending in the transmission queue and are
 * able to transmit it right now.  If so, schedule transmission.
 *
 * @param h handle to the service
 */
static void
trigger_transmit (struct GNUNET_PEERSTORE_Handle *h);

/******************************************************************************/
/*******************         CONNECTION FUNCTIONS         *********************/
/******************************************************************************/

/**
 * Task scheduled to re-try connecting to the peerstore service.
 *
 * @param cls the 'struct GNUNET_PEERSTORE_Handle'
 * @param tc scheduler context
 */
static void
reconnect_task (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct GNUNET_PEERSTORE_Handle *h = cls;

  h->r_task = GNUNET_SCHEDULER_NO_TASK;
  reconnect (h);
}

/**
 * Connect to the PEERSTORE service.
 *
 * @return NULL on error
 */
struct GNUNET_PEERSTORE_Handle *
GNUNET_PEERSTORE_connect (const struct GNUNET_CONFIGURATION_Handle *cfg)
{
  struct GNUNET_CLIENT_Connection *client;
  struct GNUNET_PEERSTORE_Handle *h;

  client = GNUNET_CLIENT_connect ("peerstore", cfg);
  if(NULL == client)
    return NULL;
  h = GNUNET_new (struct GNUNET_PEERSTORE_Handle);
  h->client = client;
  h->cfg = cfg;
  return h;
}

/**
 * Disconnect from the PEERSTORE service
 *
 * @param h handle to disconnect
 */
void
GNUNET_PEERSTORE_disconnect(struct GNUNET_PEERSTORE_Handle *h)
{
  if (NULL != h->client)
  {
    GNUNET_CLIENT_disconnect (h->client);
    h->client = NULL;
  }
  GNUNET_free (h);
}

/**
 * Close the existing connection to PEERSTORE and reconnect.
 *
 * @param h handle to the service
 */
static void
reconnect (struct GNUNET_PEERSTORE_Handle *h)
{
  if (GNUNET_SCHEDULER_NO_TASK != h->r_task)
  {
    GNUNET_SCHEDULER_cancel (h->r_task);
    h->r_task = GNUNET_SCHEDULER_NO_TASK;
  }
  if (NULL != h->th)
  {
    GNUNET_CLIENT_notify_transmit_ready_cancel (h->th);
    h->th = NULL;
  }
  if (NULL != h->client)
  {
    GNUNET_CLIENT_disconnect (h->client);
    h->client = NULL;
  }
  h->in_receive = GNUNET_NO;
  h->client = GNUNET_CLIENT_connect ("peerstore", h->cfg);
  if (NULL == h->client)
  {
    h->r_task =
        GNUNET_SCHEDULER_add_delayed (GNUNET_TIME_UNIT_SECONDS, &reconnect_task,
                                      h);
    return;
  }
  trigger_transmit (h);
}

/**
 * Transmit the request at the head of the transmission queue
 * and trigger continuation (if any).
 *
 * @param cls the 'struct GNUNET_PEERSTORE_Handle' (with the queue)
 * @param size size of the buffer (0 on error)
 * @param buf where to copy the message
 * @return number of bytes copied to buf
 */
static size_t
do_transmit (void *cls, size_t size, void *buf)
{
  struct GNUNET_PEERSTORE_Handle *h = cls;
  struct GNUNET_PEERSTORE_AddContext *ac = h->ac_head;
  size_t ret;

  h->th = NULL;
  if (NULL == ac)
    return 0; /* request was cancelled in the meantime */
  if (NULL == buf)
  {
    /* peerstore service died */
    LOG (GNUNET_ERROR_TYPE_DEBUG | GNUNET_ERROR_TYPE_BULK,
         "Failed to transmit message to `%s' service.\n", "PEERSTORE");
    GNUNET_CONTAINER_DLL_remove (h->ac_head, h->ac_tail, ac);
    reconnect (h);
    if (NULL != ac->cont)
      ac->cont (ac->cont_cls, _("failed to transmit request (service down?)"));
    GNUNET_free (ac);
    return 0;
  }
  ret = ac->size;
  if (size < ret)
  {
    /* change in head of queue (i.e. cancel + add), try again */
    trigger_transmit (h);
    return 0;
  }
  LOG (GNUNET_ERROR_TYPE_DEBUG,
       "Transmitting request of size %u to `%s' service.\n", ret, "PEERSTORE");
  memcpy (buf, &ac[1], ret);
  GNUNET_CONTAINER_DLL_remove (h->ac_head, h->ac_tail, ac);
  trigger_transmit (h);
  if (NULL != ac->cont)
    ac->cont (ac->cont_cls, NULL);
  GNUNET_free (ac);
  return ret;
}

/**
 * Check if we have a request pending in the transmission queue and are
 * able to transmit it right now.  If so, schedule transmission.
 *
 * @param h handle to the service
 */
static void
trigger_transmit (struct GNUNET_PEERSTORE_Handle *h)
{
  struct GNUNET_PEERSTORE_AddContext *ac;

  if (NULL == (ac = h->ac_head))
    return; /* no requests queued */
  if (NULL != h->th)
    return; /* request already pending */
  if (NULL == h->client)
  {
    /* disconnected, try to reconnect */
    reconnect (h);
    return;
  }
  h->th =
    GNUNET_CLIENT_notify_transmit_ready (h->client, ac->size,
           GNUNET_TIME_UNIT_FOREVER_REL,
           GNUNET_YES,
           &do_transmit, h);
}

/******************************************************************************/
/*******************             ADD FUNCTIONS            *********************/
/******************************************************************************/

struct GNUNET_PEERSTORE_AddContext *
GNUNET_PEERSTORE_add (struct GNUNET_PEERSTORE_Handle *h,
    const struct GNUNET_PeerIdentity *peer,
    const char *sub_system,
    const void *value,
    size_t size,
    struct GNUNET_TIME_Relative lifetime,
    GNUNET_PEERSTORE_Continuation cont,
    void *cont_cls)
{
  struct GNUNET_PEERSTORE_AddContext *ac;
  struct AddEntryMessage *entry;
  char *ss;
  void *val;
  size_t sub_system_size;
  size_t request_size;

  LOG (GNUNET_ERROR_TYPE_DEBUG,
      "Storing value (size: %lu) for subsytem `%s' and peer `%s'",
      size, sub_system, GNUNET_i2s (peer));
  sub_system_size = strlen(sub_system);
  request_size = sizeof(struct AddEntryMessage) + sub_system_size + size;
  ac = GNUNET_malloc(sizeof(struct GNUNET_PEERSTORE_AddContext) + request_size);
  ac->h = h;
  ac->size = request_size;
  entry = (struct AddEntryMessage *)&ac[1];
  entry->header.size = htons(request_size);
  entry->header.type = htons(GNUNET_MESSAGE_TYPE_PEERSTORE_ADD);
  entry->peer = *peer;
  entry->sub_system_size = sub_system_size;
  entry->value_size = size;
  entry->lifetime = lifetime;
  ss = (char *)&entry[1];
  memcpy(ss, sub_system, sub_system_size);
  val = ss + sub_system_size;
  memcpy(val, value, size);
  GNUNET_CONTAINER_DLL_insert_tail(h->ac_head, h->ac_tail, ac);
  trigger_transmit (h);
  return ac;

}


/* end of peerstore_api.c */
