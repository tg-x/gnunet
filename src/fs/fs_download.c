/*
     This file is part of GNUnet.
     (C) 2001, 2002, 2003, 2004, 2005, 2006, 2008, 2009 Christian Grothoff (and other contributing authors)

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
 * @file fs/fs_download.c
 * @brief download methods
 * @author Christian Grothoff
 *
 * TODO:
 * - process replies
 * - callback signaling
 * - location URI suppport (can wait)
 * - persistence (can wait)
 */
#include "platform.h"
#include "gnunet_constants.h"
#include "gnunet_fs_service.h"
#include "fs.h"

#define DEBUG_DOWNLOAD GNUNET_YES


/**
 * Schedule the download of the specified
 * block in the tree.
 *
 * @param dc overall download this block belongs to
 * @param chk content-hash-key of the block
 * @param offset offset of the block in the file
 *         (for IBlocks, the offset is the lowest
 *          offset of any DBlock in the subtree under
 *          the IBlock)
 * @param depth depth of the block, 0 is the root of the tree
 */
static void
schedule_block_download (struct GNUNET_FS_DownloadContext *dc,
			 const struct ContentHashKey *chk,
			 uint64_t offset,
			 unsigned int depth)
{
  struct DownloadRequest *sm;

  sm = GNUNET_malloc (sizeof (struct DownloadRequest));
  sm->chk = *chk;
  sm->offset = offset;
  sm->depth = depth;
  sm->is_pending = GNUNET_YES;
  sm->next = dc->pending;
  dc->pending = sm;
  GNUNET_CONTAINER_multihashmap_put (dc->active,
				     &chk->query,
				     sm,
				     GNUNET_CONTAINER_MULTIHASHMAPOPTION_MULTIPLE);
}


/**
 * We've lost our connection with the FS service.
 * Re-establish it and re-transmit all of our
 * pending requests.
 *
 * @param dc download context that is having trouble
 */
static void
try_reconnect (struct GNUNET_FS_DownloadContext *dc);


/**
 * Process a search result.
 *
 * @param sc our search context
 * @param type type of the result
 * @param data the (encrypted) response
 * @param size size of data
 */
static void
process_result (struct GNUNET_FS_DownloadContext *dc,
		uint32_t type,
		const void *data,
		size_t size)
{
  GNUNET_HashCode query;
  struct DownloadRequest *sm;
  struct GNUNET_CRYPTO_AesSessionKey skey;
  struct GNUNET_CRYPTO_AesInitializationVector iv;
  char pt[size];

  GNUNET_CRYPTO_hash (data, size, &query);
  sm = GNUNET_CONTAINER_multihashmap_get (dc->active,
					  &query);
  if (NULL == sm)
    {
      GNUNET_break (0);
      return;
    }
  GNUNET_assert (GNUNET_YES ==
		 GNUNET_CONTAINER_multihashmap_remove (dc->active,
						       &query,
						       sm));
  GNUNET_CRYPTO_hash_to_aes_key (&sm->chk.key, &skey, &iv);
  GNUNET_CRYPTO_aes_decrypt (data,
			     size,
			     &skey,
			     &iv,
			     pt);
  // FIXME: save to disk
  // FIXME: make persistent
  // FIXME: call progress callback
  // FIXME: trigger next block (if applicable)  
}


/**
 * Type of a function to call when we receive a message
 * from the service.
 *
 * @param cls closure
 * @param msg message received, NULL on timeout or fatal error
 */
static void 
receive_results (void *cls,
		 const struct GNUNET_MessageHeader * msg)
{
  struct GNUNET_FS_DownloadContext *dc = cls;
  const struct ContentMessage *cm;
  uint16_t msize;

  if ( (NULL == msg) ||
       (ntohs (msg->type) != GNUNET_MESSAGE_TYPE_FS_CONTENT) ||
       (ntohs (msg->size) <= sizeof (struct ContentMessage)) )
    {
      try_reconnect (dc);
      return;
    }
  msize = ntohs (msg->size);
  cm = (const struct ContentMessage*) msg;
  process_result (dc, 
		  ntohl (cm->type),
		  &cm[1],
		  msize - sizeof (struct ContentMessage));
  /* continue receiving */
  GNUNET_CLIENT_receive (dc->client,
			 &receive_results,
			 dc,
			 GNUNET_TIME_UNIT_FOREVER_REL);
}



/**
 * We're ready to transmit a search request to the
 * file-sharing service.  Do it.  If there is 
 * more than one request pending, try to send 
 * multiple or request another transmission.
 *
 * @param cls closure
 * @param size number of bytes available in buf
 * @param buf where the callee should write the message
 * @return number of bytes written to buf
 */
static size_t
transmit_download_request (void *cls,
			   size_t size, 
			   void *buf)
{
  struct GNUNET_FS_DownloadContext *dc = cls;
  size_t msize;
  struct SearchMessage *sm;

  if (NULL == buf)
    {
      try_reconnect (dc);
      return 0;
    }
  GNUNET_assert (size >= sizeof (struct SearchMessage));
  msize = 0;
  sm = buf;
  while ( (dc->pending == NULL) &&
	  (size > msize + sizeof (struct SearchMessage)) )
    {
      memset (sm, 0, sizeof (struct SearchMessage));
      sm->header.size = htons (sizeof (struct SearchMessage));
      sm->header.type = htons (GNUNET_MESSAGE_TYPE_FS_START_SEARCH);
      sm->anonymity_level = htonl (dc->anonymity);
      // FIXME: support 'loc' URIs (set sm->target) 
      sm->query = dc->pending->chk.query;
      dc->pending->is_pending = GNUNET_NO;
      dc->pending = dc->pending->next;
      msize += sizeof (struct SearchMessage);
      sm++;
    }
  return msize;
}


/**
 * Reconnect to the FS service and transmit
 * our queries NOW.
 *
 * @param cls our download context
 * @param tc unused
 */
static void
do_reconnect (void *cls,
	      const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct GNUNET_FS_DownloadContext *dc = cls;
  struct GNUNET_CLIENT_Connection *client;
  
  dc->task = GNUNET_SCHEDULER_NO_TASK;
  client = GNUNET_CLIENT_connect (dc->h->sched,
				  "fs",
				  dc->h->cfg);
  if (NULL == client)
    {
      try_reconnect (dc);
      return;
    }
  dc->client = client;
  GNUNET_CLIENT_notify_transmit_ready (client,
				       sizeof (struct SearchMessage),
                                       GNUNET_CONSTANTS_SERVICE_TIMEOUT,
				       &transmit_download_request,
				       dc);  
  GNUNET_CLIENT_receive (client,
			 &receive_results,
			 dc,
			 GNUNET_TIME_UNIT_FOREVER_REL);
}


/**
 * Add entries that are not yet pending back to
 * the pending list.
 *
 * @param cls our download context
 * @param key unused
 * @param entry entry of type "struct DownloadRequest"
 * @return GNUNET_OK
 */
static int
retry_entry (void *cls,
	     const GNUNET_HashCode *key,
	     void *entry)
{
  struct GNUNET_FS_DownloadContext *dc = cls;
  struct DownloadRequest *dr = entry;

  if (! dr->is_pending)
    {
      dr->next = dc->pending;
      dr->is_pending = GNUNET_YES;
      dc->pending = entry;
    }
  return GNUNET_OK;
}


/**
 * We've lost our connection with the FS service.
 * Re-establish it and re-transmit all of our
 * pending requests.
 *
 * @param dc download context that is having trouble
 */
static void
try_reconnect (struct GNUNET_FS_DownloadContext *dc)
{
  
  if (NULL != dc->client)
    {
      GNUNET_CONTAINER_multihashmap_iterate (dc->active,
					     &retry_entry,
					     dc);
      GNUNET_CLIENT_disconnect (dc->client);
      dc->client = NULL;
    }
  dc->task
    = GNUNET_SCHEDULER_add_delayed (dc->h->sched,
				    GNUNET_NO,
				    GNUNET_SCHEDULER_PRIORITY_IDLE,
				    GNUNET_SCHEDULER_NO_TASK,
				    GNUNET_TIME_UNIT_SECONDS,
				    &do_reconnect,
				    dc);
}


/**
 * Download parts of a file.  Note that this will store
 * the blocks at the respective offset in the given file.  Also, the
 * download is still using the blocking of the underlying FS
 * encoding.  As a result, the download may *write* outside of the
 * given boundaries (if offset and length do not match the 32k FS
 * block boundaries). <p>
 *
 * This function should be used to focus a download towards a
 * particular portion of the file (optimization), not to strictly
 * limit the download to exactly those bytes.
 *
 * @param h handle to the file sharing subsystem
 * @param uri the URI of the file (determines what to download); CHK or LOC URI
 * @param filename where to store the file, maybe NULL (then no file is
 *        created on disk and data must be grabbed from the callbacks)
 * @param offset at what offset should we start the download (typically 0)
 * @param length how many bytes should be downloaded starting at offset
 * @param anonymity anonymity level to use for the download
 * @param options various options
 * @param parent parent download to associate this download with (use NULL
 *        for top-level downloads; useful for manually-triggered recursive downloads)
 * @return context that can be used to control this download
 */
struct GNUNET_FS_DownloadContext *
GNUNET_FS_file_download_start (struct GNUNET_FS_Handle *h,
			       const struct GNUNET_FS_Uri *uri,
			       const char *filename,
			       uint64_t offset,
			       uint64_t length,
			       uint32_t anonymity,
			       enum GNUNET_FS_DownloadOptions options,
			       struct GNUNET_FS_DownloadContext *parent)
{
  struct GNUNET_FS_DownloadContext *dc;
  struct GNUNET_CLIENT_Connection *client;

  client = GNUNET_CLIENT_connect (h->sched,
				  "fs",
				  h->cfg);
  if (NULL == client)
    return NULL;
  // FIXME: add support for "loc" URIs!
  GNUNET_assert (GNUNET_FS_uri_test_chk (uri));
  if ( (dc->offset + dc->length < dc->offset) ||
       (dc->offset + dc->length > uri->data.chk.file_length) )
    {
      GNUNET_break (0);
      return NULL;
    }
  dc = GNUNET_malloc (sizeof(struct GNUNET_FS_DownloadContext));
  dc->h = h;
  dc->client = client;
  dc->parent = parent;
  dc->uri = GNUNET_FS_uri_dup (uri);
  dc->filename = (NULL == filename) ? NULL : GNUNET_strdup (filename);
  dc->offset = offset;
  dc->length = length;
  dc->anonymity = anonymity;
  dc->options = options;
  dc->active = GNUNET_CONTAINER_multihashmap_create (1 + (length / DBLOCK_SIZE));
  // FIXME: make persistent
  schedule_block_download (dc, 
			   &dc->uri->data.chk.chk,
			   0, 
			   0);
  GNUNET_CLIENT_notify_transmit_ready (client,
				       sizeof (struct SearchMessage),
                                       GNUNET_CONSTANTS_SERVICE_TIMEOUT,
				       &transmit_download_request,
				       dc);  
  GNUNET_CLIENT_receive (client,
			 &receive_results,
			 dc,
			 GNUNET_TIME_UNIT_FOREVER_REL);
  // FIXME: signal download start
  return dc;
}


/**
 * Free entries in the map.
 *
 * @param cls unused (NULL)
 * @param key unused
 * @param entry entry of type "struct DownloadRequest" which is freed
 * @return GNUNET_OK
 */
static int
free_entry (void *cls,
	    const GNUNET_HashCode *key,
	    void *entry)
{
  GNUNET_free (entry);
  return GNUNET_OK;
}


/**
 * Stop a download (aborts if download is incomplete).
 *
 * @param dc handle for the download
 * @param do_delete delete files of incomplete downloads
 */
void
GNUNET_FS_file_download_stop (struct GNUNET_FS_DownloadContext *dc,
			      int do_delete)
{
  // FIXME: make unpersistent
  // FIXME: signal download end
  
  if (GNUNET_SCHEDULER_NO_TASK != dc->task)
    GNUNET_SCHEDULER_cancel (dc->h->sched,
			     dc->task);
  if (NULL != dc->client)
    GNUNET_CLIENT_disconnect (dc->client);
  GNUNET_CONTAINER_multihashmap_iterate (dc->active,
					 &free_entry,
					 NULL);
  GNUNET_CONTAINER_multihashmap_destroy (dc->active);
  GNUNET_FS_uri_destroy (dc->uri);
  GNUNET_free_non_null (dc->filename);
  GNUNET_free (dc);
}


















#if 0

/**
 * Node-specific data (not shared, keep small!). 152 bytes.
 * Nodes are kept in a doubly-linked list.
 */
struct Node
{
  /**
   * Pointer to shared data between all nodes (request manager,
   * progress data, etc.).
   */
  struct GNUNET_ECRS_DownloadContext *ctx;

  /**
   * Previous entry in DLL.
   */
  struct Node *prev;

  /**
   * Next entry in DLL.
   */
  struct Node *next;

  /**
   * What is the GNUNET_EC_ContentHashKey for this block?
   */
  GNUNET_EC_ContentHashKey chk;

  /**
   * At what offset (on the respective level!) is this
   * block?
   */
  unsigned long long offset;

  /**
   * 0 for dblocks, >0 for iblocks.
   */
  unsigned int level;

};

/**
 * @brief structure that keeps track of currently pending requests for
 *        a download
 *
 * Handle to the state of a request manager.  Here we keep track of
 * which queries went out with which priorities and which nodes in
 * the merkle-tree are waiting for the replies.
 */
struct GNUNET_ECRS_DownloadContext
{

  /**
   * Total number of bytes in the file.
   */
  unsigned long long total;

  /**
   * Number of bytes already obtained
   */
  unsigned long long completed;

  /**
   * Starting-offset in file (for partial download)
   */
  unsigned long long offset;

  /**
   * Length of the download (starting at offset).
   */
  unsigned long long length;

  /**
   * Time download was started.
   */
  GNUNET_CronTime startTime;

  /**
   * Doubly linked list of all pending requests (head)
   */
  struct Node *head;

  /**
   * Doubly linked list of all pending requests (tail)
   */
  struct Node *tail;

  /**
   * FSLIB context for issuing requests.
   */
  struct GNUNET_FS_SearchContext *sctx;

  /**
   * Context for error reporting.
   */
  struct GNUNET_GE_Context *ectx;

  /**
   * Configuration information.
   */
  struct GNUNET_GC_Configuration *cfg;

  /**
   * The file handle.
   */
  int handle;

  /**
   * Do we exclusively own this sctx?
   */
  int my_sctx;

  /**
   * The base-filename
   */
  char *filename;

  /**
   * Main thread running the operation.
   */
  struct GNUNET_ThreadHandle *main;

  /**
   * Function to call when we make progress.
   */
  GNUNET_ECRS_DownloadProgressCallback dpcb;

  /**
   * Extra argument to dpcb.
   */
  void *dpcbClosure;

  /**
   * Identity of the peer having the content, or all-zeros
   * if we don't know of such a peer.
   */
  GNUNET_PeerIdentity target;

  /**
   * Abort?  Flag that can be set at any time
   * to abort the RM as soon as possible.  Set
   * to GNUNET_YES during orderly shutdown,
   * set to GNUNET_SYSERR on error.
   */
  int abortFlag;

  /**
   * Do we have a specific peer from which we download
   * from?
   */
  int have_target;

  /**
   * Desired anonymity level for the download.
   */
  unsigned int anonymityLevel;

  /**
   * The depth of the file-tree.
   */
  unsigned int treedepth;

};

static int
content_receive_callback (const GNUNET_HashCode * query,
                          const GNUNET_DatastoreValue * reply, void *cls,
                          unsigned long long uid);


/**
 * Close the files and free the associated resources.
 *
 * @param self reference to the download context
 */
static void
free_request_manager (struct GNUNET_ECRS_DownloadContext *rm)
{
  struct Node *pos;

  if (rm->abortFlag == GNUNET_NO)
    rm->abortFlag = GNUNET_YES;
  if (rm->my_sctx == GNUNET_YES)
    GNUNET_FS_destroy_search_context (rm->sctx);
  else
    GNUNET_FS_suspend_search_context (rm->sctx);
  while (rm->head != NULL)
    {
      pos = rm->head;
      GNUNET_DLL_remove (rm->head, rm->tail, pos);
      if (rm->my_sctx != GNUNET_YES)
        GNUNET_FS_stop_search (rm->sctx, &content_receive_callback, pos);
      GNUNET_free (pos);
    }
  if (rm->my_sctx != GNUNET_YES)
    GNUNET_FS_resume_search_context (rm->sctx);
  GNUNET_GE_ASSERT (NULL, rm->tail == NULL);
  if (rm->handle >= 0)
    CLOSE (rm->handle);
  if (rm->main != NULL)
    GNUNET_thread_release_self (rm->main);
  GNUNET_free_non_null (rm->filename);
  rm->sctx = NULL;
  GNUNET_free (rm);
}

/**
 * Read method.
 *
 * @param self reference to the download context
 * @param level level in the tree to read/write at
 * @param pos position where to read or write
 * @param buf where to read from or write to
 * @param len how many bytes to read or write
 * @return number of bytes read, GNUNET_SYSERR on error
 */
static int
read_from_files (struct GNUNET_ECRS_DownloadContext *self,
                 unsigned int level,
                 unsigned long long pos, void *buf, unsigned int len)
{
  if ((level > 0) || (self->handle == -1))
    return GNUNET_SYSERR;
  LSEEK (self->handle, pos, SEEK_SET);
  return READ (self->handle, buf, len);
}

/**
 * Write method.
 *
 * @param self reference to the download context
 * @param level level in the tree to write to
 * @param pos position where to  write
 * @param buf where to write to
 * @param len how many bytes to write
 * @return number of bytes written, GNUNET_SYSERR on error
 */
static int
write_to_files (struct GNUNET_ECRS_DownloadContext *self,
                unsigned int level,
                unsigned long long pos, void *buf, unsigned int len)
{
  int ret;

  if (level > 0)
    return len;                 /* lie -- no more temps */
  if (self->handle == -1)
    return len;
  LSEEK (self->handle, pos, SEEK_SET);
  ret = WRITE (self->handle, buf, len);
  if (ret != len)
    GNUNET_GE_LOG_STRERROR_FILE (self->ectx,
                                 GNUNET_GE_ERROR | GNUNET_GE_BULK |
                                 GNUNET_GE_USER, "write", self->filename);
  return ret;
}

/**
 * Queue a request for execution.
 *
 * @param rm the request manager struct from createRequestManager
 * @param node the node to call once a reply is received
 */
static void
add_request (struct Node *node)
{
  struct GNUNET_ECRS_DownloadContext *rm = node->ctx;

  GNUNET_DLL_insert (rm->head, rm->tail, node);
  GNUNET_FS_start_search (rm->sctx,
                          rm->have_target == GNUNET_NO ? NULL : &rm->target,
                          GNUNET_ECRS_BLOCKTYPE_DATA, 1,
                          &node->chk.query,
                          rm->anonymityLevel,
                          &content_receive_callback, node);
}

static void
signal_abort (struct GNUNET_ECRS_DownloadContext *rm, const char *msg)
{
  rm->abortFlag = GNUNET_SYSERR;
  if ((rm->head != NULL) && (rm->dpcb != NULL))
    rm->dpcb (rm->length + 1, 0, 0, 0, msg, 0, rm->dpcbClosure);
  GNUNET_thread_stop_sleep (rm->main);
}

/**
 * Dequeue a request.
 *
 * @param self the request manager struct from createRequestManager
 * @param node the block for which the request is canceled
 */
static void
delete_node (struct Node *node)
{
  struct GNUNET_ECRS_DownloadContext *rm = node->ctx;

  GNUNET_DLL_remove (rm->head, rm->tail, node);
  GNUNET_free (node);
  if (rm->head == NULL)
    GNUNET_thread_stop_sleep (rm->main);
}

/**
 * Compute how many bytes of data are stored in
 * this node.
 */
static unsigned int
get_node_size (const struct Node *node)
{
  unsigned int i;
  unsigned int ret;
  unsigned long long rsize;
  unsigned long long spos;
  unsigned long long epos;

  GNUNET_GE_ASSERT (node->ctx->ectx, node->offset < node->ctx->total);
  if (node->level == 0)
    {
      ret = GNUNET_ECRS_DBLOCK_SIZE;
      if (node->offset + (unsigned long long) ret > node->ctx->total)
        ret = (unsigned int) (node->ctx->total - node->offset);
#if DEBUG_DOWNLOAD
      GNUNET_GE_LOG (node->ctx->rm->ectx,
                     GNUNET_GE_DEBUG | GNUNET_GE_REQUEST | GNUNET_GE_USER,
                     "Node at offset %llu and level %d has size %u\n",
                     node->offset, node->level, ret);
#endif
      return ret;
    }
  rsize = GNUNET_ECRS_DBLOCK_SIZE;
  for (i = 0; i < node->level - 1; i++)
    rsize *= GNUNET_ECRS_CHK_PER_INODE;
  spos = rsize * (node->offset / sizeof (GNUNET_EC_ContentHashKey));
  epos = spos + rsize * GNUNET_ECRS_CHK_PER_INODE;
  if (epos > node->ctx->total)
    epos = node->ctx->total;
  ret = (epos - spos) / rsize;
  if (ret * rsize < epos - spos)
    ret++;                      /* need to round up! */
#if DEBUG_DOWNLOAD
  GNUNET_GE_LOG (node->ctx->rm->ectx,
                 GNUNET_GE_DEBUG | GNUNET_GE_REQUEST | GNUNET_GE_USER,
                 "Node at offset %llu and level %d has size %u\n",
                 node->offset, node->level,
                 ret * sizeof (GNUNET_EC_ContentHashKey));
#endif
  return ret * sizeof (GNUNET_EC_ContentHashKey);
}

/**
 * Notify client about progress.
 */
static void
notify_client_about_progress (const struct Node *node,
                              const char *data, unsigned int size)
{
  struct GNUNET_ECRS_DownloadContext *rm = node->ctx;
  GNUNET_CronTime eta;

  if ((rm->abortFlag != GNUNET_NO) || (node->level != 0))
    return;
  rm->completed += size;
  eta = GNUNET_get_time ();
  if (rm->completed > 0)
    eta = (GNUNET_CronTime) (rm->startTime +
                             (((double) (eta - rm->startTime) /
                               (double) rm->completed)) *
                             (double) rm->length);
  if (rm->dpcb != NULL)
    rm->dpcb (rm->length,
              rm->completed, eta, node->offset, data, size, rm->dpcbClosure);
}


/**
 * DOWNLOAD children of this GNUNET_EC_IBlock.
 *
 * @param node the node for which the children should be downloaded
 * @param data data for the node
 * @param size size of data
 */
static void iblock_download_children (const struct Node *node,
                                      const char *data, unsigned int size);

/**
 * Check if self block is already present on the drive.  If the block
 * is a dblock and present, the ProgressModel is notified. If the
 * block is present and it is an iblock, downloading the children is
 * triggered.
 *
 * Also checks if the block is within the range of blocks
 * that we are supposed to download.  If not, the method
 * returns as if the block is present but does NOT signal
 * progress.
 *
 * @param node that is checked for presence
 * @return GNUNET_YES if present, GNUNET_NO if not.
 */
static int
check_node_present (const struct Node *node)
{
  int res;
  int ret;
  char *data;
  unsigned int size;
  GNUNET_HashCode hc;

  size = get_node_size (node);
  /* first check if node is within range.
     For now, keeping it simple, we only do
     this for level-0 nodes */
  if ((node->level == 0) &&
      ((node->offset + size < node->ctx->offset) ||
       (node->offset >= node->ctx->offset + node->ctx->length)))
    return GNUNET_YES;
  data = GNUNET_malloc (size);
  ret = GNUNET_NO;
  res = read_from_files (node->ctx, node->level, node->offset, data, size);
  if (res == size)
    {
      GNUNET_hash (data, size, &hc);
      if (0 == memcmp (&hc, &node->chk.key, sizeof (GNUNET_HashCode)))
        {
          notify_client_about_progress (node, data, size);
          if (node->level > 0)
            iblock_download_children (node, data, size);
          ret = GNUNET_YES;
        }
    }
  GNUNET_free (data);
  return ret;
}

/**
 * DOWNLOAD children of this GNUNET_EC_IBlock.
 *
 * @param node the node that should be downloaded
 */
static void
iblock_download_children (const struct Node *node,
                          const char *data, unsigned int size)
{
  struct GNUNET_GE_Context *ectx = node->ctx->ectx;
  int i;
  struct Node *child;
  unsigned int childcount;
  const GNUNET_EC_ContentHashKey *chks;
  unsigned int levelSize;
  unsigned long long baseOffset;

  GNUNET_GE_ASSERT (ectx, node->level > 0);
  childcount = size / sizeof (GNUNET_EC_ContentHashKey);
  if (size != childcount * sizeof (GNUNET_EC_ContentHashKey))
    {
      GNUNET_GE_BREAK (ectx, 0);
      return;
    }
  if (node->level == 1)
    {
      levelSize = GNUNET_ECRS_DBLOCK_SIZE;
      baseOffset =
        node->offset / sizeof (GNUNET_EC_ContentHashKey) *
        GNUNET_ECRS_DBLOCK_SIZE;
    }
  else
    {
      levelSize =
        sizeof (GNUNET_EC_ContentHashKey) * GNUNET_ECRS_CHK_PER_INODE;
      baseOffset = node->offset * GNUNET_ECRS_CHK_PER_INODE;
    }
  chks = (const GNUNET_EC_ContentHashKey *) data;
  for (i = 0; i < childcount; i++)
    {
      child = GNUNET_malloc (sizeof (struct Node));
      child->ctx = node->ctx;
      child->chk = chks[i];
      child->offset = baseOffset + i * levelSize;
      GNUNET_GE_ASSERT (ectx, child->offset < node->ctx->total);
      child->level = node->level - 1;
      GNUNET_GE_ASSERT (ectx, (child->level != 0) ||
                        ((child->offset % GNUNET_ECRS_DBLOCK_SIZE) == 0));
      if (GNUNET_NO == check_node_present (child))
        add_request (child);
      else
        GNUNET_free (child);    /* done already! */
    }
}


/**
 * Decrypts a given data block
 *
 * @param data represents the data block
 * @param hashcode represents the key concatenated with the initial
 *        value used in the alg
 * @param result where to store the result (encrypted block)
 * @returns GNUNET_OK on success, GNUNET_SYSERR on error
 */
static int
decrypt_content (const char *data,
                 unsigned int size, const GNUNET_HashCode * hashcode,
                 char *result)
{
  GNUNET_AES_InitializationVector iv;
  GNUNET_AES_SessionKey skey;

  /* get key and init value from the GNUNET_HashCode */
  GNUNET_hash_to_AES_key (hashcode, &skey, &iv);
  return GNUNET_AES_decrypt (&skey, data, size, &iv, result);
}

/**
 * We received a GNUNET_EC_ContentHashKey reply for a block. Decrypt.  Note
 * that the caller (fslib) has already aquired the
 * RM lock (we sometimes aquire it again in callees,
 * mostly because our callees could be also be theoretically
 * called from elsewhere).
 *
 * @param cls the node for which the reply is given, freed in
 *        the function!
 * @param query the query for which reply is the answer
 * @param reply the reply
 * @return GNUNET_OK if the reply was valid, GNUNET_SYSERR on error
 */
static int
content_receive_callback (const GNUNET_HashCode * query,
                          const GNUNET_DatastoreValue * reply, void *cls,
                          unsigned long long uid)
{
  struct Node *node = cls;
  struct GNUNET_ECRS_DownloadContext *rm = node->ctx;
  struct GNUNET_GE_Context *ectx = rm->ectx;
  GNUNET_HashCode hc;
  unsigned int size;
  char *data;

  if (rm->abortFlag != GNUNET_NO)
    return GNUNET_SYSERR;
  GNUNET_GE_ASSERT (ectx,
                    0 == memcmp (query, &node->chk.query,
                                 sizeof (GNUNET_HashCode)));
  size = ntohl (reply->size) - sizeof (GNUNET_DatastoreValue);
  if ((size <= sizeof (GNUNET_EC_DBlock)) ||
      (size - sizeof (GNUNET_EC_DBlock) != get_node_size (node)))
    {
      GNUNET_GE_BREAK (ectx, 0);
      return GNUNET_SYSERR;     /* invalid size! */
    }
  size -= sizeof (GNUNET_EC_DBlock);
  data = GNUNET_malloc (size);
  if (GNUNET_SYSERR ==
      decrypt_content ((const char *)
                       &((const GNUNET_EC_DBlock *) &reply[1])[1], size,
                       &node->chk.key, data))
    GNUNET_GE_ASSERT (ectx, 0);
  GNUNET_hash (data, size, &hc);
  if (0 != memcmp (&hc, &node->chk.key, sizeof (GNUNET_HashCode)))
    {
      GNUNET_free (data);
      GNUNET_GE_BREAK (ectx, 0);
      signal_abort (rm,
                    _("Decrypted content does not match key. "
                      "This is either a bug or a maliciously inserted "
                      "file. Download aborted.\n"));
      return GNUNET_SYSERR;
    }
  if (size != write_to_files (rm, node->level, node->offset, data, size))
    {
      GNUNET_GE_LOG_STRERROR (ectx,
                              GNUNET_GE_ERROR | GNUNET_GE_ADMIN |
                              GNUNET_GE_USER | GNUNET_GE_BULK, "WRITE");
      signal_abort (rm, _("IO error."));
      return GNUNET_SYSERR;
    }
  notify_client_about_progress (node, data, size);
  if (node->level > 0)
    iblock_download_children (node, data, size);
  GNUNET_free (data);
  /* request satisfied, stop requesting! */
  delete_node (node);
  return GNUNET_OK;
}


/**
 * Helper function to sanitize filename
 * and create necessary directories.
 */
static char *
get_real_download_filename (struct GNUNET_GE_Context *ectx,
                            const char *filename)
{
  struct stat buf;
  char *realFN;
  char *path;
  char *pos;

  if ((filename[strlen (filename) - 1] == '/') ||
      (filename[strlen (filename) - 1] == '\\'))
    {
      realFN =
        GNUNET_malloc (strlen (filename) + strlen (GNUNET_DIRECTORY_EXT));
      strcpy (realFN, filename);
      realFN[strlen (filename) - 1] = '\0';
      strcat (realFN, GNUNET_DIRECTORY_EXT);
    }
  else
    {
      realFN = GNUNET_strdup (filename);
    }
  path = GNUNET_malloc (strlen (realFN) * strlen (GNUNET_DIRECTORY_EXT) + 1);
  strcpy (path, realFN);
  pos = path;
  while (*pos != '\0')
    {
      if (*pos == DIR_SEPARATOR)
        {
          *pos = '\0';
          if ((0 == STAT (path, &buf)) && (!S_ISDIR (buf.st_mode)))
            {
              *pos = DIR_SEPARATOR;
              memmove (pos + strlen (GNUNET_DIRECTORY_EXT),
                       pos, strlen (pos));
              memcpy (pos,
                      GNUNET_DIRECTORY_EXT, strlen (GNUNET_DIRECTORY_EXT));
              pos += strlen (GNUNET_DIRECTORY_EXT);
            }
          else
            {
              *pos = DIR_SEPARATOR;
            }
        }
      pos++;
    }
  GNUNET_free (realFN);
  return path;
}

/* ***************** main method **************** */


/**
 * Download parts of a file.  Note that this will store
 * the blocks at the respective offset in the given file.
 * Also, the download is still using the blocking of the
 * underlying ECRS encoding.  As a result, the download
 * may *write* outside of the given boundaries (if offset
 * and length do not match the 32k ECRS block boundaries).
 * <p>
 *
 * This function should be used to focus a download towards a
 * particular portion of the file (optimization), not to strictly
 * limit the download to exactly those bytes.
 *
 * @param uri the URI of the file (determines what to download)
 * @param filename where to store the file
 * @param no_temporaries set to GNUNET_YES to disallow generation of temporary files
 * @param start starting offset
 * @param length length of the download (starting at offset)
 */
struct GNUNET_ECRS_DownloadContext *
GNUNET_ECRS_file_download_partial_start (struct GNUNET_GE_Context *ectx,
                                         struct GNUNET_GC_Configuration *cfg,
                                         struct GNUNET_FS_SearchContext *sc,
                                         const struct GNUNET_ECRS_URI *uri,
                                         const char *filename,
                                         unsigned long long offset,
                                         unsigned long long length,
                                         unsigned int anonymityLevel,
                                         int no_temporaries,
                                         GNUNET_ECRS_DownloadProgressCallback
                                         dpcb, void *dpcbClosure)
{
  struct GNUNET_ECRS_DownloadContext *rm;
  struct stat buf;
  struct Node *top;
  int ret;

  if ((!GNUNET_ECRS_uri_test_chk (uri)) && (!GNUNET_ECRS_uri_test_loc (uri)))
    {
      GNUNET_GE_BREAK (ectx, 0);
      return NULL;
    }
  rm = GNUNET_malloc (sizeof (struct GNUNET_ECRS_DownloadContext));
  memset (rm, 0, sizeof (struct GNUNET_ECRS_DownloadContext));
  if (sc == NULL)
    {
      rm->sctx = GNUNET_FS_create_search_context (ectx, cfg);
      if (rm->sctx == NULL)
        {
          GNUNET_free (rm);
          return NULL;
        }
      rm->my_sctx = GNUNET_YES;
    }
  else
    {
      rm->sctx = sc;
      rm->my_sctx = GNUNET_NO;
    }
  rm->ectx = ectx;
  rm->cfg = cfg;
  rm->startTime = GNUNET_get_time ();
  rm->anonymityLevel = anonymityLevel;
  rm->offset = offset;
  rm->length = length;
  rm->dpcb = dpcb;
  rm->dpcbClosure = dpcbClosure;
  rm->main = GNUNET_thread_get_self ();
  rm->total = GNUNET_ntohll (uri->data.fi.file_length);
  rm->filename =
    filename != NULL ? get_real_download_filename (ectx, filename) : NULL;

  if ((rm->filename != NULL) &&
      (GNUNET_SYSERR ==
       GNUNET_disk_directory_create_for_file (ectx, rm->filename)))
    {
      free_request_manager (rm);
      return NULL;
    }
  if (0 == rm->total)
    {
      if (rm->filename != NULL)
        {
          ret = GNUNET_disk_file_open (ectx,
                                       rm->filename,
                                       O_CREAT | O_WRONLY | O_TRUNC,
                                       S_IRUSR | S_IWUSR);
          if (ret == -1)
            {
              free_request_manager (rm);
              return NULL;
            }
          CLOSE (ret);
        }
      dpcb (0, 0, rm->startTime, 0, NULL, 0, dpcbClosure);
      free_request_manager (rm);
      return NULL;
    }
  rm->treedepth = GNUNET_ECRS_compute_depth (rm->total);
  if ((NULL != rm->filename) &&
      ((0 == STAT (rm->filename, &buf))
       && ((size_t) buf.st_size > rm->total)))
    {
      /* if exists and oversized, truncate */
      if (truncate (rm->filename, rm->total) != 0)
        {
          GNUNET_GE_LOG_STRERROR_FILE (ectx,
                                       GNUNET_GE_ERROR | GNUNET_GE_ADMIN |
                                       GNUNET_GE_BULK, "truncate",
                                       rm->filename);
          free_request_manager (rm);
          return NULL;
        }
    }
  if (rm->filename != NULL)
    {
      rm->handle = GNUNET_disk_file_open (ectx,
                                          rm->filename,
                                          O_CREAT | O_RDWR,
                                          S_IRUSR | S_IWUSR);
      if (rm->handle < 0)
        {
          free_request_manager (rm);
          return NULL;
        }
    }
  else
    rm->handle = -1;
  if (GNUNET_ECRS_uri_test_loc (uri))
    {
      GNUNET_hash (&uri->data.loc.peer, sizeof (GNUNET_RSA_PublicKey),
                   &rm->target.hashPubKey);
      rm->have_target = GNUNET_YES;
    }
  top = GNUNET_malloc (sizeof (struct Node));
  memset (top, 0, sizeof (struct Node));
  top->ctx = rm;
  top->chk = uri->data.fi.chk;
  top->offset = 0;
  top->level = rm->treedepth;
  if (GNUNET_NO == check_node_present (top))
    add_request (top);
  else
    GNUNET_free (top);
  return rm;
}

int
GNUNET_ECRS_file_download_partial_stop (struct GNUNET_ECRS_DownloadContext
                                        *rm)
{
  int ret;

  ret = rm->abortFlag;
  free_request_manager (rm);
  if (ret == GNUNET_NO)
    ret = GNUNET_OK;            /* normal termination */
  return ret;
}

/**
 * Download parts of a file.  Note that this will store
 * the blocks at the respective offset in the given file.
 * Also, the download is still using the blocking of the
 * underlying ECRS encoding.  As a result, the download
 * may *write* outside of the given boundaries (if offset
 * and length do not match the 32k ECRS block boundaries).
 * <p>
 *
 * This function should be used to focus a download towards a
 * particular portion of the file (optimization), not to strictly
 * limit the download to exactly those bytes.
 *
 * @param uri the URI of the file (determines what to download)
 * @param filename where to store the file
 * @param no_temporaries set to GNUNET_YES to disallow generation of temporary files
 * @param start starting offset
 * @param length length of the download (starting at offset)
 */
int
GNUNET_ECRS_file_download_partial (struct GNUNET_GE_Context *ectx,
                                   struct GNUNET_GC_Configuration *cfg,
                                   const struct GNUNET_ECRS_URI *uri,
                                   const char *filename,
                                   unsigned long long offset,
                                   unsigned long long length,
                                   unsigned int anonymityLevel,
                                   int no_temporaries,
                                   GNUNET_ECRS_DownloadProgressCallback dpcb,
                                   void *dpcbClosure,
                                   GNUNET_ECRS_TestTerminate tt,
                                   void *ttClosure)
{
  struct GNUNET_ECRS_DownloadContext *rm;
  int ret;

  if (length == 0)
    return GNUNET_OK;
  rm = GNUNET_ECRS_file_download_partial_start (ectx,
                                                cfg,
                                                NULL,
                                                uri,
                                                filename,
                                                offset,
                                                length,
                                                anonymityLevel,
                                                no_temporaries,
                                                dpcb, dpcbClosure);
  if (rm == NULL)
    return GNUNET_SYSERR;
  while ((GNUNET_OK == tt (ttClosure)) &&
         (GNUNET_YES != GNUNET_shutdown_test ()) &&
         (rm->abortFlag == GNUNET_NO) && (rm->head != NULL))
    GNUNET_thread_sleep (5 * GNUNET_CRON_SECONDS);
  ret = GNUNET_ECRS_file_download_partial_stop (rm);
  return ret;
}

/**
 * Download a file (simplified API).
 *
 * @param uri the URI of the file (determines what to download)
 * @param filename where to store the file
 */
int
GNUNET_ECRS_file_download (struct GNUNET_GE_Context *ectx,
                           struct GNUNET_GC_Configuration *cfg,
                           const struct GNUNET_ECRS_URI *uri,
                           const char *filename,
                           unsigned int anonymityLevel,
                           GNUNET_ECRS_DownloadProgressCallback dpcb,
                           void *dpcbClosure, GNUNET_ECRS_TestTerminate tt,
                           void *ttClosure)
{
  return GNUNET_ECRS_file_download_partial (ectx,
                                            cfg,
                                            uri,
                                            filename,
                                            0,
                                            GNUNET_ECRS_uri_get_file_size
                                            (uri), anonymityLevel, GNUNET_NO,
                                            dpcb, dpcbClosure, tt, ttClosure);
}

#endif

/* end of fs_download.c */
