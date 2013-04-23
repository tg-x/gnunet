/*
      This file is part of GNUnet
      (C) 2008--2013 Christian Grothoff (and other contributing authors)

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
 * @file testbed/testbed_logger_api.c
 * @brief Client-side routines for communicating with the tesbted logger service
 * @author Sree Harsha Totakura <sreeharsha@totakura.in> 
 */

#include "platform.h"
#include "gnunet_util_lib.h"
#include "gnunet_testbed_logger_service.h"

/**
 * Generic logging shorthand
 */
#define LOG(kind, ...)                          \
  GNUNET_log_from (kind, "testbed-logger-api", __VA_ARGS__)

/**
 * Debug logging
 */
#define LOG_DEBUG(...)                          \
  LOG (GNUNET_ERROR_TYPE_DEBUG, __VA_ARGS__)

#ifdef GNUNET_TIME_STD_EXPONENTIAL_BACKOFF_THRESHOLD
#undef GNUNET_TIME_STD_EXPONENTIAL_BACKOFF_THRESHOLD
#endif

/**
 * Threshold after which exponential backoff should not increase (15 s).
 */
#define GNUNET_TIME_STD_EXPONENTIAL_BACKOFF_THRESHOLD GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS, 3)


/**
 * The message queue for sending messages to the controller service
 */
struct MessageQueue
{
  /**
   * next pointer for DLL
   */
  struct MessageQueue *next;

  /**
   * prev pointer for DLL
   */
  struct MessageQueue *prev;

  /**
   * The message to be sent
   */
  struct GNUNET_MessageHeader *msg;

  /**
   * Completion callback
   */
  GNUNET_TESTBED_LOGGER_FlushCompletion cb;

  /**
   * callback closure
   */
  void *cb_cls;
};


/**
 * Connection handle for the logger service
 */
struct GNUNET_TESTBED_LOGGER_Handle
{
  /**
   * Client connection
   */
  struct GNUNET_CLIENT_Connection *client;

  struct GNUNET_CLIENT_TransmitHandle *th;

  /**
   * DLL head for the message queue
   */
  struct MessageQueue *mq_head;

  /**
   * DLL tail for the message queue
   */
  struct MessageQueue *mq_tail;

  GNUNET_SCHEDULER_TaskIdentifier flush_completion_task;

  GNUNET_TESTBED_LOGGER_FlushCompletion cb;

  void *cb_cls;

  void *buf;

  size_t bs;

  size_t bwrote;

  struct GNUNET_TIME_Relative retry_backoff;
};


/**
 * Task to call the flush completion notification
 *
 * @param cls the logger handle
 * @param tc the scheduler task context
 */
static void
call_flush_completion (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct GNUNET_TESTBED_LOGGER_Handle *h = cls; 
  GNUNET_TESTBED_LOGGER_FlushCompletion cb;
  void *cb_cls;
  size_t bw;

  h->flush_completion_task = GNUNET_SCHEDULER_NO_TASK;
  bw = h->bwrote;
  h->bwrote = 0;
  cb = h->cb;
  h->cb = NULL;
  cb_cls = h->cb_cls;
  h->cb_cls = NULL;
  if (NULL != cb)
    cb (cb_cls, bw);
}


/**
 * Schedule the flush completion notification task
 *
 * @param h logger handle
 */
static void
trigger_flush_notification (struct GNUNET_TESTBED_LOGGER_Handle *h)
{
  if (GNUNET_SCHEDULER_NO_TASK != h->flush_completion_task)
    GNUNET_SCHEDULER_cancel (h->flush_completion_task);
  h->flush_completion_task = GNUNET_SCHEDULER_add_now (&call_flush_completion, h);
}


/**
 * Function called to notify a client about the connection begin ready to queue
 * more data.  "buf" will be NULL and "size" zero if the connection was closed
 * for writing in the meantime.
 *
 * @param cls closure
 * @param size number of bytes available in buf
 * @param buf where the callee should write the message
 * @return number of bytes written to buf
 */
static size_t
transmit_ready_notify (void *cls, size_t size, void *buf)
{
  struct GNUNET_TESTBED_LOGGER_Handle *h = cls;
  struct MessageQueue *mq;

  h->th = NULL;
  mq = h->mq_head;
  GNUNET_assert (NULL != mq);
  if ((0 == size) && (NULL == buf))     /* Timeout */
  {
    LOG_DEBUG ("Message sending timed out -- retrying\n");
    h->retry_backoff = GNUNET_TIME_STD_BACKOFF (h->retry_backoff);
    h->th =
        GNUNET_CLIENT_notify_transmit_ready (h->client,
                                             ntohs (mq->msg->size),
                                             h->retry_backoff, GNUNET_YES,
                                             &transmit_ready_notify, h);
    return 0;
  }
  h->retry_backoff = GNUNET_TIME_UNIT_ZERO;
  GNUNET_assert (ntohs (mq->msg->size) <= size);
  size = ntohs (mq->msg->size);
  memcpy (buf, mq->msg, size);
  LOG_DEBUG ("Message of type: %u and size: %u sent\n",
             ntohs (mq->msg->type), size);
  GNUNET_free (mq->msg);
  GNUNET_CONTAINER_DLL_remove (h->mq_head, h->mq_tail, mq);
  GNUNET_free (mq);
  h->bwrote += (size - sizeof (struct GNUNET_MessageHeader));
  mq = h->mq_head;
  if (NULL != mq)
  {
    h->retry_backoff = GNUNET_TIME_STD_BACKOFF (h->retry_backoff);
    h->th =
        GNUNET_CLIENT_notify_transmit_ready (h->client,
                                             ntohs (mq->msg->size),
                                             h->retry_backoff, GNUNET_YES,
                                             &transmit_ready_notify, h);
    return size;
  }
  if (NULL != h->cb)
    trigger_flush_notification (h);       /* Call the flush completion callback */
  return size;
}


/**
 * Queues a message in send queue of the logger handle
 *
 * @param h the logger handle
 * @param msg the message to queue
 */
static void
queue_message (struct GNUNET_TESTBED_LOGGER_Handle *h,
               struct GNUNET_MessageHeader *msg)
{
  struct MessageQueue *mq;
  uint16_t type;
  uint16_t size;

  type = ntohs (msg->type);
  size = ntohs (msg->size);
  mq = GNUNET_malloc (sizeof (struct MessageQueue));
  mq->msg = msg;
  LOG (GNUNET_ERROR_TYPE_DEBUG,
       "Queueing message of type %u, size %u for sending\n", type,
       ntohs (msg->size));
  GNUNET_CONTAINER_DLL_insert_tail (h->mq_head, h->mq_tail, mq);
  if (NULL == h->th)
  {
    h->retry_backoff = GNUNET_TIME_STD_BACKOFF (h->retry_backoff);
    h->th =
        GNUNET_CLIENT_notify_transmit_ready (h->client, size,
                                             h->retry_backoff, GNUNET_YES,
                                             &transmit_ready_notify,
                                             h);
  }
}


/**
 * Send the buffered data to the service
 *
 * @param h the logger handle
 */
static void
dispatch_buffer (struct GNUNET_TESTBED_LOGGER_Handle *h)
{
  struct GNUNET_MessageHeader *msg;
  size_t msize;

  msize = sizeof (struct GNUNET_MessageHeader) + h->bs;
  msg = GNUNET_realloc (h->buf, msize);
  h->buf = NULL;
  memmove (&msg[1], msg, h->bs);
  h->bs = 0;    
  msg->type = htons (GNUNET_MESSAGE_TYPE_TESTBED_LOGGER_MSG);
  msg->size = htons (msize);
  queue_message (h, msg);
}


/**
 * Connect to the testbed logger service
 *
 * @param cfg configuration to use
 * @return the handle which can be used for sending data to the service; NULL
 *           upon any error
 */
struct GNUNET_TESTBED_LOGGER_Handle *
GNUNET_TESTBED_LOGGER_connect (const struct GNUNET_CONFIGURATION_Handle *cfg)
{
  struct GNUNET_TESTBED_LOGGER_Handle *h;
  struct GNUNET_CLIENT_Connection *client;
  
  client = GNUNET_CLIENT_connect ("testbed-logger", cfg);
  if (NULL == client)
    return NULL;
  h = GNUNET_malloc (sizeof (struct GNUNET_TESTBED_LOGGER_Handle));
  h->client = client;
  return h;
}


/**
 * Disconnect from the logger service.
 *
 * @param h the logger handle
 */
void
GNUNET_TESTBED_LOGGER_disconnect (struct GNUNET_TESTBED_LOGGER_Handle *h)
{
  struct MessageQueue *mq;

  if (GNUNET_SCHEDULER_NO_TASK != h->flush_completion_task)
    GNUNET_SCHEDULER_cancel (h->flush_completion_task);
  while (NULL != (mq = h->mq_head))
  {
    GNUNET_CONTAINER_DLL_remove (h->mq_head, h->mq_tail, mq);
    GNUNET_free (mq->msg);
    GNUNET_free (mq);
  }
  GNUNET_CLIENT_disconnect (h->client);
  GNUNET_free (h);
}


/**
 * Send data to be logged to the logger service.  The data will be buffered and
 * will be sent upon an explicit call to GNUNET_TESTBED_LOGGER_flush() or upon
 * exceeding a threshold size.
 *
 * @param h the logger handle
 * @param data the data to send;
 * @param size how many bytes of data to send
 */
void
GNUNET_TESTBED_LOGGER_write (struct GNUNET_TESTBED_LOGGER_Handle *h,
                             const void *data, size_t size)
{  
  size_t fit_size;

  GNUNET_assert (0 != size);
  GNUNET_assert (NULL != data);
  GNUNET_assert (size < (GNUNET_SERVER_MAX_MESSAGE_SIZE
                         - sizeof (struct GNUNET_MessageHeader)));
  fit_size = sizeof (struct GNUNET_MessageHeader) + h->bs + size;
  if ( GNUNET_SERVER_MAX_MESSAGE_SIZE < fit_size )
    dispatch_buffer (h);
  if (NULL == h->buf)
  {
    h->buf = GNUNET_malloc (size);
    h->bs = size;
    memcpy (h->buf, data, size);
    return;
  }
  h->buf = GNUNET_realloc (h->buf, h->bs + size);
  memcpy (h->buf + h->bs, data, size);
  h->bs += size;
  return;
}


/**
 * Flush the buffered data to the logger service
 *
 * @param h the logger handle
 * @param cb the callback to call after the data is flushed
 * @param cb_cls the closure for the above callback
 */
void
GNUNET_TESTBED_LOGGER_flush (struct GNUNET_TESTBED_LOGGER_Handle *h,
                             GNUNET_TESTBED_LOGGER_FlushCompletion cb,
                             void *cb_cls)
{
  h->cb = cb;
  h->cb_cls = cb_cls;
  if (NULL == h->buf)
  {
    trigger_flush_notification (h);
    return;
  }
  dispatch_buffer (h);
}


/**
 * Cancel notification upon flush.
 *
 * @param h the logger handle
 */
void
GNUNET_TESTBED_LOGGER_flush_cancel (struct GNUNET_TESTBED_LOGGER_Handle *h)
{
  if (GNUNET_SCHEDULER_NO_TASK != h->flush_completion_task)
  {
    GNUNET_SCHEDULER_cancel (h->flush_completion_task);
    h->flush_completion_task = GNUNET_SCHEDULER_NO_TASK;
  }
  h->cb = NULL;
  h->cb_cls = NULL;
}

/* End of testbed_logger_api.c */
