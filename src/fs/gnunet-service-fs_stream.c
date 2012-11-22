/*
     This file is part of GNUnet.
     (C) 2012 Christian Grothoff (and other contributing authors)

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
 * @file fs/gnunet-service-fs_stream.c
 * @brief non-anonymous file-transfer
 * @author Christian Grothoff
 *
 * TODO:
 * - limit # concurrent clients, have timeouts for server-side
 * - stream shutdown in callbacks from stream may not always work right now (check with stream_api!)
 */
#include "platform.h"
#include "gnunet_constants.h"
#include "gnunet_util_lib.h"
#include "gnunet_stream_lib.h"
#include "gnunet_protocols.h"
#include "gnunet_applications.h"
#include "gnunet-service-fs.h"
#include "gnunet-service-fs_indexing.h"
#include "gnunet-service-fs_stream.h"

/**
 * Information we keep around for each active streaming client.
 */
struct StreamClient
{
  /**
   * DLL
   */ 
  struct StreamClient *next;

  /**
   * DLL
   */ 
  struct StreamClient *prev;

  /**
   * Socket for communication.
   */ 
  struct GNUNET_STREAM_Socket *socket;

  /**
   * Handle for active read operation, or NULL.
   */ 
  struct GNUNET_STREAM_IOReadHandle *rh;

  /**
   * Handle for active write operation, or NULL.
   */ 
  struct GNUNET_STREAM_IOWriteHandle *wh;
  
  /**
   * Tokenizer for requests.
   */
  struct GNUNET_SERVER_MessageStreamTokenizer *mst;
  
  /**
   * Current active request to the datastore, if we have one pending.
   */
  struct GNUNET_DATASTORE_QueueEntry *qe;

  /**
   * Size of the last write that was initiated.
   */ 
  size_t reply_size;

};


/**
 * Query from one peer, asking the other for CHK-data.
 */
struct StreamQueryMessage
{

  /**
   * Type is GNUNET_MESSAGE_TYPE_FS_STREAM_QUERY.
   */
  struct GNUNET_MessageHeader header;

  /**
   * Block type must be DBLOCK or IBLOCK.
   */
  uint32_t type;

  /**
   * Query hash from CHK (hash of encrypted block).
   */
  struct GNUNET_HashCode query;

};


/**
 * Reply to a StreamQueryMessage.
 */
struct StreamReplyMessage
{

  /**
   * Type is GNUNET_MESSAGE_TYPE_FS_STREAM_REPLY.
   */
  struct GNUNET_MessageHeader header;

  /**
   * Block type must be DBLOCK or IBLOCK.
   */
  uint32_t type;

  /**
   * Expiration time for the block.
   */
  struct GNUNET_TIME_AbsoluteNBO expiration;

  /* followed by the encrypted block */

};


/** 
 * Handle for a stream to another peer.
 */
struct StreamHandle;


/**
 * Handle for a request that is going out via stream API.
 */
struct GSF_StreamRequest
{

  /**
   * DLL.
   */
  struct GSF_StreamRequest *next;

  /**
   * DLL.
   */
  struct GSF_StreamRequest *prev;

  /**
   * Which stream is this request associated with?
   */
  struct StreamHandle *sh;

  /**
   * Function to call with the result.
   */
  GSF_StreamReplyProcessor proc;

  /**
   * Closure for 'proc'
   */
  void *proc_cls;

  /**
   * Query to transmit to the other peer.
   */
  struct GNUNET_HashCode query;

  /**
   * Desired type for the reply.
   */
  enum GNUNET_BLOCK_Type type;

  /**
   * Did we transmit this request already? YES if we are
   * in the 'waiting' DLL, NO if we are in the 'pending' DLL.
   */
  int was_transmitted;
};


/** 
 * Handle for a stream to another peer.
 */
struct StreamHandle
{
  /**
   * Head of DLL of pending requests on this stream.
   */
  struct GSF_StreamRequest *pending_head;

  /**
   * Tail of DLL of pending requests on this stream.
   */
  struct GSF_StreamRequest *pending_tail;

  /**
   * Head of DLL of requests waiting for a reply on this stream.
   */
  struct GSF_StreamRequest *waiting_head;

  /**
   * Tail of DLL of requests waiting for a reply on this stream.
   */
  struct GSF_StreamRequest *waiting_tail;

  /**
   * Connection to the other peer.
   */
  struct GNUNET_STREAM_Socket *stream;

  /**
   * Handle for active read operation, or NULL.
   */ 
  struct GNUNET_STREAM_IOReadHandle *rh;

  /**
   * Handle for active write operation, or NULL.
   */ 
  struct GNUNET_STREAM_IOWriteHandle *wh;

  /**
   * Tokenizer for replies.
   */
  struct GNUNET_SERVER_MessageStreamTokenizer *mst;

  /**
   * Which peer does this stream go to?
   */ 
  struct GNUNET_PeerIdentity target;

  /**
   * Task to kill inactive streams (we keep them around for
   * a few seconds to give the application a chance to give
   * us another query).
   */
  GNUNET_SCHEDULER_TaskIdentifier timeout_task;

  /**
   * Is this stream ready for transmission?
   */
  int is_ready;

};


/**
 * Listen socket for incoming requests.
 */
static struct GNUNET_STREAM_ListenSocket *listen_socket;

/**
 * Head of DLL of stream clients.
 */ 
static struct StreamClient *sc_head;

/**
 * Tail of DLL of stream clients.
 */ 
static struct StreamClient *sc_tail;

/**
 * Map from peer identities to 'struct StreamHandles' with streams to
 * those peers.
 */
static struct GNUNET_CONTAINER_MultiHashMap *stream_map;


/* ********************* client-side code ************************* */


/**
 * Destroy a stream handle.
 *
 * @param sh stream to process
 */
static void
destroy_stream_handle (struct StreamHandle *sh)
{
  struct GSF_StreamRequest *sr;

  while (NULL != (sr = sh->pending_head))
  {
    sr->proc (sr->proc_cls, GNUNET_BLOCK_TYPE_ANY,
	      GNUNET_TIME_UNIT_FOREVER_ABS,
	      0, NULL);
    GSF_stream_query_cancel (sr);
  }
  while (NULL != (sr = sh->waiting_head))
  {
    sr->proc (sr->proc_cls, GNUNET_BLOCK_TYPE_ANY,
	      GNUNET_TIME_UNIT_FOREVER_ABS,
	      0, NULL);
    GSF_stream_query_cancel (sr);
  }
  if (NULL != sh->wh)
    GNUNET_STREAM_io_write_cancel (sh->wh);
  if (NULL != sh->rh)
    GNUNET_STREAM_io_read_cancel (sh->rh);
  if (GNUNET_SCHEDULER_NO_TASK != sh->timeout_task)
    GNUNET_SCHEDULER_cancel (sh->timeout_task);
  GNUNET_STREAM_close (sh->stream);
  GNUNET_assert (GNUNET_OK ==
		 GNUNET_CONTAINER_multihashmap_remove (stream_map,
						       &sh->target.hashPubKey,
						       sh));
  GNUNET_free (sh);
}


/**
 * Transmit pending requests via the stream.
 *
 * @param sh stream to process
 */
static void
transmit_pending (struct StreamHandle *sh);


/**
 * Function called once the stream is ready for transmission.
 *
 * @param cls the 'struct StreamHandle'
 * @param socket stream socket handle
 */
static void
stream_ready_cb (void *cls,
		 struct GNUNET_STREAM_Socket *socket)
{
  struct StreamHandle *sh = cls;

  sh->is_ready = GNUNET_YES;
  transmit_pending (sh);
}


/**
 * We had a serious error, tear down and re-create stream from scratch.
 *
 * @param sh stream to reset
 */
static void
reset_stream (struct StreamHandle *sh)
{
  struct GSF_StreamRequest *sr;
  
  if (NULL != sh->rh)
    GNUNET_STREAM_io_read_cancel (sh->rh);
  GNUNET_STREAM_close (sh->stream);
  sh->is_ready = GNUNET_NO;
  while (NULL != (sr = sh->waiting_tail))
  {
    GNUNET_CONTAINER_DLL_remove (sh->waiting_head,
				 sh->waiting_tail,
				 sr);
    GNUNET_CONTAINER_DLL_insert (sh->pending_head,
				 sh->pending_tail,
				 sr);
    sr->was_transmitted = GNUNET_NO;
  }
  sh->stream = GNUNET_STREAM_open (GSF_cfg,
				   &sh->target,
				   GNUNET_APPLICATION_TYPE_FS_BLOCK_TRANSFER,
				   &stream_ready_cb, sh,
				   GNUNET_STREAM_OPTION_END);
}


/**
 * We got a reply from the stream.  Process it.
 *
 * @param cls the struct StreamHandle 
 * @param status the status of the stream at the time this function is called
 * @param data traffic from the other side
 * @param size the number of bytes available in data read; will be 0 on timeout 
 * @return number of bytes of processed from 'data' (any data remaining should be
 *         given to the next time the read processor is called).
 */
static size_t
handle_stream_reply (void *cls,
		     enum GNUNET_STREAM_Status status,
		     const void *data,
		     size_t size)
{
  struct StreamHandle *sh = cls;

  sh->rh = NULL;
  if (GNUNET_SYSERR == 
      GNUNET_SERVER_mst_receive (sh->mst,
				 NULL,
				 data, size,
				 GNUNET_NO, GNUNET_NO))
  {
    GNUNET_break_op (0);
    reset_stream (sh);
    return size;
  }
  sh->rh = GNUNET_STREAM_read (sh->stream,
			       GNUNET_TIME_UNIT_FOREVER_REL,
			       &handle_stream_reply,
			       sh);
  return size;
}


/**
 * Functions of this signature are called whenever we transmitted a
 * query via a stream.
 *
 * @param cls the struct StreamHandle for which we did the write call
 * @param status the status of the stream at the time this function is called;
 *          GNUNET_OK if writing to stream was completed successfully,
 *          GNUNET_STREAM_SHUTDOWN if the stream is shutdown for writing in the
 *          mean time.
 * @param size the number of bytes written
 */
static void
query_write_continuation (void *cls,
			  enum GNUNET_STREAM_Status status,
			  size_t size)
{
  struct StreamHandle *sh = cls;

  sh->wh = NULL;
  if ( (GNUNET_STREAM_OK != status) ||
       (sizeof (struct StreamQueryMessage) != size) )
  {
    reset_stream (sh);
    return;
  }
  if (NULL == sh->rh)
    sh->rh = GNUNET_STREAM_read (sh->stream,
				 GNUNET_TIME_UNIT_FOREVER_REL,
				 &handle_stream_reply,
				 sh);
  transmit_pending (sh);
}
	  

/**
 * Transmit pending requests via the stream.
 *
 * @param sh stream to process
 */
static void
transmit_pending (struct StreamHandle *sh)
{
  struct StreamQueryMessage sqm;
  struct GSF_StreamRequest *sr;

  if (NULL != sh->wh)
    return;
  sr = sh->pending_head;
  if (NULL == sr)
    return;
  GNUNET_CONTAINER_DLL_remove (sh->pending_head,
			       sh->pending_tail,
			       sr);
  GNUNET_CONTAINER_DLL_insert_tail (sh->waiting_head,
				    sh->waiting_tail,
				    sr);
  sr->was_transmitted = GNUNET_YES;
  sqm.header.size = htons (sizeof (sqm));
  sqm.header.type = htons (GNUNET_MESSAGE_TYPE_FS_STREAM_QUERY);
  sqm.type = htonl (sr->type);
  sqm.query = sr->query;
  sh->wh = GNUNET_STREAM_write (sh->stream,
				&sqm, sizeof (sqm),
				GNUNET_TIME_UNIT_FOREVER_REL,
				&query_write_continuation,
				sh);
}


/**
 * Functions with this signature are called whenever a
 * complete reply is received.
 *
 * Do not call GNUNET_SERVER_mst_destroy in callback
 *
 * @param cls closure with the 'struct StreamHandle'
 * @param client identification of the client, NULL
 * @param message the actual message
 * @return GNUNET_OK on success, GNUNET_SYSERR to stop further processing
 */
static int
reply_cb (void *cls,
	  void *client,
	  const struct GNUNET_MessageHeader *message)
{
  struct StreamHandle *sh = cls;
  const struct StreamReplyMessage *srm;
  uint16_t msize;
  enum GNUNET_BLOCK_Type type;
  struct GNUNET_HashCode query;
  struct GSF_StreamRequest *sr;

  msize = ntohs (message->size);
  switch (ntohs (message->type))
  {
  case GNUNET_MESSAGE_TYPE_FS_STREAM_REPLY:
    if (sizeof (struct StreamReplyMessage) > msize)
    {
      GNUNET_break_op (0);
      return GNUNET_SYSERR;
    }
    srm = (const struct StreamReplyMessage *) message;
    msize -= sizeof (struct StreamReplyMessage);
    type = (enum GNUNET_BLOCK_Type) ntohl (srm->type);
    if (GNUNET_YES !=
	GNUNET_BLOCK_get_key (GSF_block_ctx,
			      type,
			      &srm[1], msize, &query))
    {
      GNUNET_break_op (0);
      return GNUNET_SYSERR;
    }
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		"Received reply `%s' via stream\n",
		GNUNET_h2s (&query));
    GNUNET_STATISTICS_update (GSF_stats,
			      gettext_noop ("# replies received via stream"), 1,
			      GNUNET_NO);
    for (sr = sh->waiting_head; NULL != sr; sr = sr->next)
      if (0 == memcmp (&query,
		       &sr->query,
		       sizeof (struct GNUNET_HashCode)))
	break;
    if (NULL == sr)
    {
      GNUNET_STATISTICS_update (GSF_stats,
				gettext_noop ("# replies received via stream dropped"), 1,
				GNUNET_NO);
      return GNUNET_OK;
    }
    sr->proc (sr->proc_cls,
	      type,
	      GNUNET_TIME_absolute_ntoh (srm->expiration),
	      msize,
	      &srm[1]);
    GSF_stream_query_cancel (sr);
    return GNUNET_OK;
  default:
    GNUNET_break_op (0);
    return GNUNET_SYSERR;
  }
}


/**
 * Get (or create) a stream to talk to the given peer.
 *
 * @param target peer we want to communicate with
 */
static struct StreamHandle *
get_stream (const struct GNUNET_PeerIdentity *target)
{
  struct StreamHandle *sh;

  sh = GNUNET_CONTAINER_multihashmap_get (stream_map,
					  &target->hashPubKey);
  if (NULL != sh)
  {
    if (GNUNET_SCHEDULER_NO_TASK != sh->timeout_task)
    {
      GNUNET_SCHEDULER_cancel (sh->timeout_task);
      sh->timeout_task = GNUNET_SCHEDULER_NO_TASK;
    }
    return sh;
  }
  sh = GNUNET_malloc (sizeof (struct StreamHandle));
  sh->mst = GNUNET_SERVER_mst_create (&reply_cb,
				      sh);
  sh->target = *target;
  sh->stream = GNUNET_STREAM_open (GSF_cfg,
				   &sh->target,
				   GNUNET_APPLICATION_TYPE_FS_BLOCK_TRANSFER,
				   &stream_ready_cb, sh,
				   GNUNET_STREAM_OPTION_END);
  GNUNET_assert (GNUNET_OK ==
		 GNUNET_CONTAINER_multihashmap_put (stream_map,
						    &sh->target.hashPubKey,
						    sh,
						    GNUNET_CONTAINER_MULTIHASHMAPOPTION_UNIQUE_ONLY));
  return sh;
}


/**
 * Look for a block by directly contacting a particular peer.
 *
 * @param target peer that should have the block
 * @param query hash to query for the block
 * @param type desired type for the block
 * @param proc function to call with result
 * @param proc_cls closure for 'proc'
 * @return handle to cancel the operation
 */
struct GSF_StreamRequest *
GSF_stream_query (const struct GNUNET_PeerIdentity *target,
		  const struct GNUNET_HashCode *query,
		  enum GNUNET_BLOCK_Type type,
		  GSF_StreamReplyProcessor proc, void *proc_cls)
{
  struct StreamHandle *sh;
  struct GSF_StreamRequest *sr;

  sh = get_stream (target);
  sr = GNUNET_malloc (sizeof (struct GSF_StreamRequest));
  sr->sh = sh;
  sr->proc = proc;
  sr->proc_cls = proc_cls;
  sr->type = type;
  sr->query = *query;
  GNUNET_CONTAINER_DLL_insert (sh->pending_head,
			       sh->pending_tail,
			       sr);
  if (GNUNET_YES == sh->is_ready)
    transmit_pending (sh);
  return sr;
}


/**
 * Task called when it is time to destroy an inactive stream.
 *
 * @param cls the 'struct StreamHandle' to tear down
 * @param tc scheduler context, unused
 */
static void
stream_timeout (void *cls,
		const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct StreamHandle *sh = cls;

  sh->timeout_task = GNUNET_SCHEDULER_NO_TASK;
  destroy_stream_handle (sh);
}


/**
 * Cancel an active request; must not be called after 'proc'
 * was calld.
 *
 * @param sr request to cancel
 */
void
GSF_stream_query_cancel (struct GSF_StreamRequest *sr)
{
  struct StreamHandle *sh = sr->sh;

  if (GNUNET_YES == sr->was_transmitted)
    GNUNET_CONTAINER_DLL_remove (sh->waiting_head,
				 sh->waiting_tail,
				 sr);
  else
    GNUNET_CONTAINER_DLL_remove (sh->pending_head,
				 sh->pending_tail,
				 sr);
  GNUNET_free (sr);
  if ( (NULL == sh->waiting_head) &&
       (NULL == sh->pending_head) )
    sh->timeout_task = GNUNET_SCHEDULER_add_delayed (GNUNET_TIME_UNIT_SECONDS,
						     &stream_timeout,
						     sh);
}


/* ********************* server-side code ************************* */


/**
 * We're done with a particular client, clean up.
 *
 * @param sc client to clean up
 */
static void
terminate_stream (struct StreamClient *sc)
{
  GNUNET_STATISTICS_update (GSF_stats,
			    gettext_noop ("# stream connections active"), -1,
			    GNUNET_NO);
  if (NULL != sc->rh)
    GNUNET_STREAM_io_read_cancel (sc->rh);
  if (NULL != sc->wh)
    GNUNET_STREAM_io_write_cancel (sc->wh);
  if (NULL != sc->qe)
    GNUNET_DATASTORE_cancel (sc->qe);
  GNUNET_SERVER_mst_destroy (sc->mst);
  GNUNET_STREAM_close (sc->socket);
  GNUNET_CONTAINER_DLL_remove (sc_head,
			       sc_tail,
			       sc);
  GNUNET_free (sc);
}


/**
 * Functions of this signature are called whenever data is available from the
 * stream.
 *
 * @param cls the closure from GNUNET_STREAM_read
 * @param status the status of the stream at the time this function is called
 * @param data traffic from the other side
 * @param size the number of bytes available in data read; will be 0 on timeout 
 * @return number of bytes of processed from 'data' (any data remaining should be
 *         given to the next time the read processor is called).
 */
static size_t 
process_request (void *cls,
		 enum GNUNET_STREAM_Status status,
		 const void *data,
		 size_t size);


/**
 * We're done handling a request from a client, read the next one.
 *
 * @param sc client to continue reading requests from
 */
static void
continue_reading (struct StreamClient *sc)
{
  int ret;

  ret = 
    GNUNET_SERVER_mst_receive (sc->mst,
			       NULL,
			       NULL, 0,
			       GNUNET_NO, GNUNET_YES);
  if (GNUNET_NO == ret)
    return; 
  sc->rh = GNUNET_STREAM_read (sc->socket,
			       GNUNET_TIME_UNIT_FOREVER_REL,
			       &process_request,
			       sc);      
}


/**
 * Functions of this signature are called whenever data is available from the
 * stream.
 *
 * @param cls the closure from GNUNET_STREAM_read
 * @param status the status of the stream at the time this function is called
 * @param data traffic from the other side
 * @param size the number of bytes available in data read; will be 0 on timeout 
 * @return number of bytes of processed from 'data' (any data remaining should be
 *         given to the next time the read processor is called).
 */
static size_t 
process_request (void *cls,
		 enum GNUNET_STREAM_Status status,
		 const void *data,
		 size_t size)
{
  struct StreamClient *sc = cls;
  int ret;

  sc->rh = NULL;
  switch (status)
  {
  case GNUNET_STREAM_OK:
    ret = 
      GNUNET_SERVER_mst_receive (sc->mst,
				 NULL,
				 data, size,
				 GNUNET_NO, GNUNET_YES);
    if (GNUNET_NO == ret)
      return size; /* more messages in MST */
    if (GNUNET_SYSERR == ret)
    {
      GNUNET_break_op (0);
      terminate_stream (sc);
      return size;
    }
    break;
  case GNUNET_STREAM_TIMEOUT:
  case GNUNET_STREAM_SHUTDOWN:
  case GNUNET_STREAM_SYSERR:
  case GNUNET_STREAM_BROKEN:
    terminate_stream (sc);
    return size;
  default:
    GNUNET_break (0);
    return size;
  }
  continue_reading (sc);
  return size;
}


/**
 * Sending a reply was completed, continue processing.
 *
 * @param cls closure with the struct StreamClient which sent the query
 */
static void
write_continuation (void *cls,
		    enum GNUNET_STREAM_Status status,
		    size_t size)
{
  struct StreamClient *sc = cls;
  
  sc->wh = NULL;
  if ( (GNUNET_STREAM_OK == status) &&
       (size == sc->reply_size) )
  {
    GNUNET_STATISTICS_update (GSF_stats,
			      gettext_noop ("# Blocks transferred via stream"), 1,
			      GNUNET_NO);
    continue_reading (sc);
  }
  else
    terminate_stream (sc);    
}


/**
 * Process a datum that was stored in the datastore.
 *
 * @param cls closure with the struct StreamClient which sent the query
 * @param key key for the content
 * @param size number of bytes in data
 * @param data content stored
 * @param type type of the content
 * @param priority priority of the content
 * @param anonymity anonymity-level for the content
 * @param expiration expiration time for the content
 * @param uid unique identifier for the datum;
 *        maybe 0 if no unique identifier is available
 */
static void 
handle_datastore_reply (void *cls,
			const struct GNUNET_HashCode * key,
			size_t size, const void *data,
			enum GNUNET_BLOCK_Type type,
			uint32_t priority,
			uint32_t anonymity,
			struct GNUNET_TIME_Absolute
			expiration, uint64_t uid)
{
  struct StreamClient *sc = cls;
  size_t msize = size + sizeof (struct StreamReplyMessage);
  char buf[msize] GNUNET_ALIGN;
  struct StreamReplyMessage *srm = (struct StreamReplyMessage *) buf;

  sc->qe = NULL;
  if (GNUNET_BLOCK_TYPE_FS_ONDEMAND == type)
  {
    if (GNUNET_OK !=
	GNUNET_FS_handle_on_demand_block (key,
					  size, data, type,
					  priority, anonymity,
					  expiration, uid,
					  &handle_datastore_reply,
					  sc))
    {
      continue_reading (sc);
    }
    return;
  }
  if (msize > GNUNET_SERVER_MAX_MESSAGE_SIZE)
  {
    GNUNET_break (0);
    continue_reading (sc);
    return;
  }
  srm->header.size = htons ((uint16_t) msize);
  srm->header.type = htons (GNUNET_MESSAGE_TYPE_FS_STREAM_REPLY);
  srm->type = htonl (type);
  srm->expiration = GNUNET_TIME_absolute_hton (expiration);
  memcpy (&srm[1], data, size);
  sc->reply_size = msize;
  sc->wh = GNUNET_STREAM_write (sc->socket,
				buf, msize,
				GNUNET_TIME_UNIT_FOREVER_REL,
				&write_continuation,
				sc);
  if (NULL == sc->wh)
  {
    terminate_stream (sc);
    return;
  }
}


/**
 * Functions with this signature are called whenever a
 * complete query message is received.
 *
 * Do not call GNUNET_SERVER_mst_destroy in callback
 *
 * @param cls closure with the 'struct StreamClient'
 * @param client identification of the client, NULL
 * @param message the actual message
 * @return GNUNET_OK on success, GNUNET_SYSERR to stop further processing
 */
static int
request_cb (void *cls,
	    void *client,
	    const struct GNUNET_MessageHeader *message)
{
  struct StreamClient *sc = cls;
  const struct StreamQueryMessage *sqm;

  switch (ntohs (message->type))
  {
  case GNUNET_MESSAGE_TYPE_FS_STREAM_QUERY:
    if (sizeof (struct StreamQueryMessage) != 
	ntohs (message->size))
    {
      GNUNET_break_op (0);
      return GNUNET_SYSERR;
    }
    sqm = (const struct StreamQueryMessage *) message;
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		"Received query for `%s' via stream\n",
		GNUNET_h2s (&sqm->query));
    GNUNET_STATISTICS_update (GSF_stats,
			      gettext_noop ("# queries received via stream"), 1,
			      GNUNET_NO);
    sc->qe = GNUNET_DATASTORE_get_key (GSF_dsh,
				       0,
				       &sqm->query,
				       ntohl (sqm->type),
				       0 /* priority */, 
				       GSF_datastore_queue_size,
				       GNUNET_TIME_UNIT_FOREVER_REL,
				       &handle_datastore_reply, sc);
    if (NULL == sc->qe)
      continue_reading (sc);
    return GNUNET_OK;
  default:
    GNUNET_break_op (0);
    return GNUNET_SYSERR;
  }
}


/**
 * Functions of this type are called upon new stream connection from other peers
 * or upon binding error which happen when the app_port given in
 * GNUNET_STREAM_listen() is already taken.
 *
 * @param cls the closure from GNUNET_STREAM_listen
 * @param socket the socket representing the stream; NULL on binding error
 * @param initiator the identity of the peer who wants to establish a stream
 *            with us; NULL on binding error
 * @return GNUNET_OK to keep the socket open, GNUNET_SYSERR to close the
 *             stream (the socket will be invalid after the call)
 */
static int 
accept_cb (void *cls,
	   struct GNUNET_STREAM_Socket *socket,
	   const struct GNUNET_PeerIdentity *initiator)
{
  struct StreamClient *sc;

  if (NULL == socket)
    return GNUNET_SYSERR;
  GNUNET_STATISTICS_update (GSF_stats,
			    gettext_noop ("# stream connections active"), 1,
			    GNUNET_NO);
  sc = GNUNET_malloc (sizeof (struct StreamClient));
  sc->socket = socket;
  sc->mst = GNUNET_SERVER_mst_create (&request_cb,
				      sc);
  sc->rh = GNUNET_STREAM_read (sc->socket,
			       GNUNET_TIME_UNIT_FOREVER_REL,
			       &process_request,
			       sc);
  GNUNET_CONTAINER_DLL_insert (sc_head,
			       sc_tail,
			       sc);
  return GNUNET_OK;
}


/**
 * Initialize subsystem for non-anonymous file-sharing.
 */
void
GSF_stream_start ()
{
  stream_map = GNUNET_CONTAINER_multihashmap_create (16, GNUNET_YES);
  listen_socket = GNUNET_STREAM_listen (GSF_cfg,
					GNUNET_APPLICATION_TYPE_FS_BLOCK_TRANSFER,
					&accept_cb, NULL,
					GNUNET_STREAM_OPTION_END);
}


/**
 * Function called on each active streams to shut them down.
 *
 * @param cls NULL
 * @param key target peer, unused
 * @param value the 'struct StreamHandle' to destroy
 * @return GNUNET_YES (continue to iterate)
 */
static int
release_streams (void *cls,
		 const struct GNUNET_HashCode *key,
		 void *value)
{
  struct StreamHandle *sh = value;

  destroy_stream_handle (sh);
  return GNUNET_YES;
}


/**
 * Shutdown subsystem for non-anonymous file-sharing.
 */
void
GSF_stream_stop ()
{
  struct StreamClient *sc;

  while (NULL != (sc = sc_head))
    terminate_stream (sc);
  if (NULL != listen_socket)
  {
    GNUNET_STREAM_listen_close (listen_socket);
    listen_socket = NULL;
  }
  GNUNET_CONTAINER_multihashmap_iterate (stream_map,
					 &release_streams,
					 NULL);
  GNUNET_CONTAINER_multihashmap_destroy (stream_map);
  stream_map = NULL;
}

/* end of gnunet-service-fs_stream.c */
