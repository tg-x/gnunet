/*
     This file is part of GNUnet.
     (C) 2009, 2010 Christian Grothoff (and other contributing authors)

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
 * @file dv/dv_api.c
 * @brief library to access the DV service
 * @author Christian Grothoff
 * @author Nathan Evans
 */
#include "platform.h"
#include "gnunet_bandwidth_lib.h"
#include "gnunet_client_lib.h"
#include "gnunet_constants.h"
#include "gnunet_container_lib.h"
#include "gnunet_arm_service.h"
#include "gnunet_hello_lib.h"
#include "gnunet_protocols.h"
#include "gnunet_server_lib.h"
#include "gnunet_time_lib.h"
#include "gnunet_dv_service.h"
#include "dv.h"


struct PendingMessages
{
  /**
   * Linked list of pending messages
   */
  struct PendingMessages *next;

  /**
   * Message that is pending
   */
  struct GNUNET_DV_SendMessage *msg;

  /**
   * Timeout for this message
   */
  struct GNUNET_TIME_Absolute timeout;

};



/**
 * Handle for the service.
 */
struct GNUNET_DV_Handle
{
  /**
   * Our scheduler.
   */
  struct GNUNET_SCHEDULER_Handle *sched;

  /**
   * Configuration to use.
   */
  const struct GNUNET_CONFIGURATION_Handle *cfg;

  /**
   * Socket (if available).
   */
  struct GNUNET_CLIENT_Connection *client;

  /**
   * Currently pending transmission request.
   */
  struct GNUNET_CLIENT_TransmitHandle *th;

  /**
   * List of the currently pending messages for the DV service.
   */
  struct PendingMessages *pending_list;

  /**
   * Message we are currently sending.
   */
  struct PendingMessages *current;

  /**
   * Kill off the connection and any pending messages.
   */
  int do_destroy;

  /**
   * Handler for messages we receive from the DV service
   */
  GNUNET_DV_MessageReceivedHandler receive_handler;

  /**
   * Closure for the receive handler
   */
  void *receive_cls;

};


struct StartContext
{

  /**
   * Start message
   */
  struct GNUNET_MessageHeader *message;

  /**
   * Handle to service, in case of timeout
   */
  struct GNUNET_DV_Handle *handle;
};


/**
 * Try to (re)connect to the dv service.
 *
 * @return GNUNET_YES on success, GNUNET_NO on failure.
 */
static int
try_connect (struct GNUNET_DV_Handle *ret)
{
  if (ret->client != NULL)
    return GNUNET_OK;
  ret->client = GNUNET_CLIENT_connect (ret->sched, "dv", ret->cfg);
  if (ret->client != NULL)
    return GNUNET_YES;
#if DEBUG_STATISTICS
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              _("Failed to connect to the dv service!\n"));
#endif
  return GNUNET_NO;
}

static void process_pending_message(struct GNUNET_DV_Handle *handle);

/**
 * Send complete, schedule next
 */
static void
finish (struct GNUNET_DV_Handle *handle, int code)
{
  struct PendingMessages *pos = handle->current;
  handle->current = NULL;
  process_pending_message (handle);

  GNUNET_free (pos);
}


static size_t
transmit_pending (void *cls, size_t size, void *buf)
{
  struct GNUNET_DV_Handle *handle = cls;
  size_t ret;
  size_t tsize;

  if (buf == NULL)
    {
      finish(handle, GNUNET_SYSERR);
      return 0;
    }
  handle->th = NULL;

  ret = 0;

  if (handle->current != NULL)
  {
    tsize = ntohs(handle->current->msg->header.size);
    if (size >= tsize)
    {
      memcpy(buf, handle->current->msg, tsize);
    }
    else
    {
      return ret;
    }
  }

  return ret;
}

/**
 * Try to send messages from list of messages to send
 */
static void process_pending_message(struct GNUNET_DV_Handle *handle)
{
  struct GNUNET_TIME_Relative timeout;

  if (handle->current != NULL)
    return;                     /* action already pending */
  if (GNUNET_YES != try_connect (handle))
    {
      finish (handle, GNUNET_SYSERR);
      return;
    }

  /* schedule next action */
  handle->current = handle->pending_list;
  if (NULL == handle->current)
    {
      if (handle->do_destroy)
        {
          handle->do_destroy = GNUNET_NO;
          //GNUNET_DV_disconnect (handle); /* FIXME: replace with proper disconnect stuffs */
        }
      return;
    }
  handle->pending_list = handle->pending_list->next;
  handle->current->next = NULL;

  timeout = GNUNET_TIME_absolute_get_remaining (handle->current->timeout);
  if (NULL ==
      (handle->th = GNUNET_CLIENT_notify_transmit_ready (handle->client,
                                                    ntohs(handle->current->msg->msgbuf_size),
                                                    timeout,
                                                    GNUNET_YES,
                                                    &transmit_pending, handle)))
    {
#if DEBUG_DV
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "Failed to transmit request to dv service.\n");
#endif
      finish (handle, GNUNET_SYSERR);
    }
}

/**
 * Add a pending message to the linked list
 *
 * @param handle handle to the specified DV api
 * @param msg the message to add to the list
 */
static void add_pending(struct GNUNET_DV_Handle *handle, struct GNUNET_DV_SendMessage *msg)
{
  struct PendingMessages *new_message;
  struct PendingMessages *pos;
  struct PendingMessages *last;

  new_message = GNUNET_malloc(sizeof(struct PendingMessages));
  new_message->msg = msg;

  if (handle->pending_list != NULL)
    {
      pos = handle->pending_list;
      while(pos != NULL)
        {
          last = pos;
          pos = pos->next;
        }
      new_message->next = last->next; /* Should always be null */
      last->next = new_message;
    }
  else
    {
      new_message->next = handle->pending_list; /* Will always be null */
      handle->pending_list = new_message;
    }

  process_pending_message(handle);
}


void handle_message_receipt (void *cls,
                             const struct GNUNET_MessageHeader * msg)
{
  struct GNUNET_DV_Handle *handle = cls;
  struct GNUNET_DV_MessageReceived *received_msg;
  size_t packed_msg_len;
  size_t sender_address_len;
  char *sender_address;
  char *packed_msg;

  if (msg == NULL)
  {
    return; /* Connection closed? */
  }

  GNUNET_assert(ntohs(msg->type) == GNUNET_MESSAGE_TYPE_TRANSPORT_DV_RECEIVE);

  if (ntohs(msg->size) < sizeof(struct GNUNET_DV_MessageReceived))
    return;

  received_msg = (struct GNUNET_DV_MessageReceived *)msg;
  packed_msg_len = ntohs(received_msg->msg_len);
  sender_address_len = ntohs(received_msg->sender_address_len);
#if DEBUG_DV
  fprintf(stdout, "dv api receives message from service: total len: %lu, packed len: %lu, sender_address_len: %lu, base message len: %lu\ntotal is %lu, should be %lu\n", ntohs(msg->size), packed_msg_len, sender_address_len, sizeof(struct GNUNET_DV_MessageReceived), sizeof(struct GNUNET_DV_MessageReceived) + packed_msg_len + sender_address_len, ntohs(msg->size));
#endif
  GNUNET_assert(ntohs(msg->size) == (sizeof(struct GNUNET_DV_MessageReceived) + packed_msg_len + sender_address_len));

  sender_address = GNUNET_malloc(sender_address_len);
  memcpy(sender_address, &received_msg[1], sender_address_len);
  packed_msg = GNUNET_malloc(packed_msg_len);
  memcpy(packed_msg, &received_msg[1 + sender_address_len], packed_msg_len);

  handle->receive_handler(handle->receive_cls,
                          &received_msg->sender,
                          packed_msg,
                          packed_msg_len,
                          ntohl(received_msg->distance),
                          sender_address,
                          sender_address_len);

  GNUNET_free(sender_address);

  GNUNET_CLIENT_receive (handle->client,
                         &handle_message_receipt,
                         handle, GNUNET_TIME_UNIT_FOREVER_REL);
}

/**
 * Send a message from the plugin to the DV service indicating that
 * a message should be sent via DV to some peer.
 *
 * @param dv_handle the handle to the DV api
 * @param target the final target of the message
 * @param msgbuf the msg(s) to send
 * @param msgbuf_size the size of msgbuf
 * @param priority priority to pass on to core when sending the message
 * @param timeout how long can this message be delayed (pass through to core)
 * @param addr the address of this peer (internally known to DV)
 * @param addrlen the length of the peer address
 *
 */
int GNUNET_DV_send (struct GNUNET_DV_Handle *dv_handle,
                    const struct GNUNET_PeerIdentity *target,
                    const char *msgbuf,
                    size_t msgbuf_size,
                    unsigned int priority,
                    struct GNUNET_TIME_Relative timeout,
                    const void *addr,
                    size_t addrlen)
{
  struct GNUNET_DV_SendMessage *msg;

  msg = GNUNET_malloc(sizeof(struct GNUNET_DV_SendMessage) + msgbuf_size + addrlen);
  msg->header.size = htons(sizeof(struct GNUNET_DV_SendMessage) + msgbuf_size + addrlen);
  msg->header.type = htons(GNUNET_MESSAGE_TYPE_TRANSPORT_DV_SEND);
  memcpy(&msg->target, target, sizeof(struct GNUNET_PeerIdentity));
  msg->msgbuf = GNUNET_malloc(msgbuf_size);
  memcpy(msg->msgbuf, msgbuf, msgbuf_size);
  msg->msgbuf_size = htons(msgbuf_size);
  msg->priority = htonl(priority);
  msg->timeout = timeout;
  msg->addrlen = htons(addrlen);
  memcpy(&msg[1], addr, addrlen);

  add_pending(dv_handle, msg);

  return GNUNET_OK;
}

/* Forward declaration */
void GNUNET_DV_disconnect(struct GNUNET_DV_Handle *handle);

static size_t
transmit_start (void *cls, size_t size, void *buf)
{
  struct StartContext *start_context = cls;
  struct GNUNET_DV_Handle *handle = start_context->handle;
  size_t tsize;

  if (buf == NULL)
    {
      GNUNET_free(start_context->message);
      GNUNET_free(start_context);
      GNUNET_DV_disconnect(handle);
      return 0;
    }

  tsize = ntohs(start_context->message->size);
  if (size >= tsize)
  {
    memcpy(buf, start_context->message, tsize);
    return tsize;
  }

  return 0;
}

/**
 * Connect to the DV service
 *
 * @param sched the scheduler to use
 * @param cfg the configuration to use
 * @param receive_handler method call when on receipt from the service
 * @param receive_handler_cls closure for receive_handler
 *
 * @return handle to the DV service
 */
struct GNUNET_DV_Handle *
GNUNET_DV_connect (struct GNUNET_SCHEDULER_Handle *sched,
                  const struct GNUNET_CONFIGURATION_Handle *cfg,
                  GNUNET_DV_MessageReceivedHandler receive_handler,
                  void *receive_handler_cls)
{
  struct GNUNET_DV_Handle *handle;
  struct GNUNET_MessageHeader *start_message;
  struct StartContext *start_context;
  handle = GNUNET_malloc(sizeof(struct GNUNET_DV_Handle));

  handle->cfg = cfg;
  handle->sched = sched;
  handle->pending_list = NULL;
  handle->current = NULL;
  handle->do_destroy = GNUNET_NO;
  handle->th = NULL;
  handle->client = GNUNET_CLIENT_connect(sched, "dv", cfg);
  handle->receive_handler = receive_handler;
  handle->receive_cls = receive_handler_cls;

  if (handle->client == NULL)
    {
      GNUNET_free(handle);
      return NULL;
    }

  start_message = GNUNET_malloc(sizeof(struct GNUNET_MessageHeader));
  start_message->size = htons(sizeof(struct GNUNET_MessageHeader));
  start_message->type = htons(GNUNET_MESSAGE_TYPE_DV_START);

  start_context = GNUNET_malloc(sizeof(struct StartContext));
  start_context->handle = handle;
  start_context->message = start_message;
  GNUNET_CLIENT_notify_transmit_ready (handle->client,
                                       sizeof(struct GNUNET_MessageHeader),
                                       GNUNET_TIME_relative_multiply(GNUNET_TIME_UNIT_SECONDS, 60),
                                       GNUNET_YES,
                                       &transmit_start, start_context);

  GNUNET_CLIENT_receive (handle->client,
                         &handle_message_receipt,
                         handle, GNUNET_TIME_UNIT_FOREVER_REL);

  return handle;
}

/**
 * Disconnect from the DV service
 *
 * @param handle the current handle to the service to disconnect
 */
void GNUNET_DV_disconnect(struct GNUNET_DV_Handle *handle)
{
  struct PendingMessages *pos;

  GNUNET_assert(handle != NULL);

  if (handle->th != NULL) /* We have a live transmit request in the Aether */
    {
      GNUNET_CLIENT_notify_transmit_ready_cancel (handle->th);
      handle->th = NULL;
    }
  if (handle->current != NULL) /* We are trying to send something now, clean it up */
    GNUNET_free(handle->current);
  while (NULL != (pos = handle->pending_list)) /* Remove all pending sends from the list */
    {
      handle->pending_list = pos->next;
      GNUNET_free(pos);
    }
  if (handle->client != NULL) /* Finally, disconnect from the service */
    {
      GNUNET_CLIENT_disconnect (handle->client, GNUNET_NO);
      handle->client = NULL;
    }

  GNUNET_free (handle);
}

/* end of dv_api.c */
