/*
     This file is part of GNUnet.
     (C) 2013 Christian Grothoff (and other contributing authors)

     GNUnet is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public Liceidentity as published
     by the Free Software Foundation; either version 3, or (at your
     option) any later version.

     GNUnet is distributed in the hope that it will be useful, but
     WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     General Public Liceidentity for more details.

     You should have received a copy of the GNU General Public Liceidentity
     along with GNUnet; see the file COPYING.  If not, write to the
     Free Software Foundation, Inc., 59 Temple Place - Suite 330,
     Boston, MA 02111-1307, USA.
*/

/**
 * @file identity/identity_api.c
 * @brief api to interact with the identity service
 * @author Christian Grothoff
 */
#include "platform.h"
#include "gnunet_client_lib.h"
#include "gnunet_constants.h"
#include "gnunet_container_lib.h"
#include "gnunet_arm_service.h"
#include "gnunet_hello_lib.h"
#include "gnunet_protocols.h"
#include "gnunet_server_lib.h"
#include "gnunet_time_lib.h"
#include "gnunet_identity_service.h"
#include "identity.h"

#define LOG(kind,...) GNUNET_log_from (kind, "identity-api",__VA_ARGS__)

/** 
 * Handle for a ego.
 */
struct GNUNET_IDENTITY_Ego
{
  /**
   * Private key associated with this ego.
   */
  struct GNUNET_CRYPTO_EccPrivateKey *pk;

  /**
   * Current identifier (name) associated with this ego.
   */
  char *identifier;

  /**
   * Client context associated with this ego.
   */
  void *ctx;

  /**
   * Hash of the public key of this ego.
   */
  struct GNUNET_HashCode id;
};


/** 
 * Handle for an operation with the identity service.
 */
struct GNUNET_IDENTITY_Operation
{

  /**
   * Main identity handle.
   */
  struct GNUNET_IDENTITY_Handle *h;
  
  /**
   * We keep operations in a DLL.
   */
  struct GNUNET_IDENTITY_Operation *next;

  /**
   * We keep operations in a DLL.
   */
  struct GNUNET_IDENTITY_Operation *prev;

  /**
   * Message to send to the identity service.
   * Allocated at the end of this struct.
   */
  const struct GNUNET_MessageHeader *msg;

  /**
   * Continuation to invoke with the result of the transmission; 'cb'
   * will be NULL in this case.
   */
  GNUNET_IDENTITY_Continuation cont;

  /**
   * Continuation to invoke with the result of the transmission for
   * 'get' operations ('cont' will be NULL in this case).
   */
  GNUNET_IDENTITY_Callback cb;

  /**
   * Closure for 'cont' or 'cb'.
   */
  void *cls;

};


/**
 * Handle for the service.
 */
struct GNUNET_IDENTITY_Handle
{
  /**
   * Configuration to use.
   */
  const struct GNUNET_CONFIGURATION_Handle *cfg;

  /**
   * Socket (if available).
   */
  struct GNUNET_CLIENT_Connection *client;

  /**
   * Hash map from the hash of the public key to the
   * respective 'GNUNET_IDENTITY_Ego' handle.
   */
  struct GNUNET_CONTAINER_MultiHashMap *egos;

  /**
   * Function to call when we receive updates.
   */
  GNUNET_IDENTITY_Callback cb;

  /**
   * Closure for 'cb'.
   */
  void *cb_cls;

  /**
   * Head of active operations.
   */ 
  struct GNUNET_IDENTITY_Operation *op_head;

  /**
   * Tail of active operations.
   */ 
  struct GNUNET_IDENTITY_Operation *op_tail;

  /**
   * Currently pending transmission request, or NULL for none.
   */
  struct GNUNET_CLIENT_TransmitHandle *th;

  /**
   * Task doing exponential back-off trying to reconnect.
   */
  GNUNET_SCHEDULER_TaskIdentifier reconnect_task;

  /**
   * Time for next connect retry.
   */
  struct GNUNET_TIME_Relative reconnect_delay;

  /**
   * Are we polling for incoming messages right now?
   */
  int in_receive;

};



/**
 * Try again to connect to network size estimation service.
 *
 * @param cls the handle to the transport service
 * @param tc scheduler context
 */
static void
reconnect (void *cls,
	   const struct GNUNET_SCHEDULER_TaskContext *tc);


/**
 * Reschedule a connect attempt to the service.
 *
 * @param h transport service to reconnect
 */
static void
reschedule_connect (struct GNUNET_IDENTITY_Handle *h)
{
  GNUNET_assert (h->reconnect_task == GNUNET_SCHEDULER_NO_TASK);

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
  LOG (GNUNET_ERROR_TYPE_DEBUG,
       "Scheduling task to reconnect to identity service in %s.\n",
       GNUNET_STRINGS_relative_time_to_string (h->reconnect_delay, GNUNET_YES));
  h->reconnect_task =
      GNUNET_SCHEDULER_add_delayed (h->reconnect_delay, &reconnect, h);
  h->reconnect_delay = GNUNET_TIME_STD_BACKOFF (h->reconnect_delay);
}


/**
 * Type of a function to call when we receive a message
 * from the service.
 *
 * @param cls closure
 * @param msg message received, NULL on timeout or fatal error
 */
static void
message_handler (void *cls, 
		 const struct GNUNET_MessageHeader *msg)
{
  struct GNUNET_IDENTITY_Handle *h = cls;
  struct GNUNET_IDENTITY_Operation *op;
  struct GNUNET_IDENTITY_Ego *ego;
  const struct GNUNET_IDENTITY_ResultCodeMessage *rcm;
  const struct GNUNET_IDENTITY_UpdateMessage *um;
  const struct GNUNET_IDENTITY_SetDefaultMessage *sdm;
  struct GNUNET_CRYPTO_EccPrivateKey *priv;
  struct GNUNET_CRYPTO_EccPublicKeyBinaryEncoded pub;
  struct GNUNET_HashCode id;
  const char *str;
  uint16_t size;
  uint16_t pk_len;
  uint16_t name_len;

  if (NULL == msg)
  {
    reschedule_connect (h);
    return;
  }
  size = ntohs (msg->size);
  switch (ntohs (msg->type))
  {
  case GNUNET_MESSAGE_TYPE_IDENTITY_RESULT_CODE:
    if (size < sizeof (struct GNUNET_IDENTITY_ResultCodeMessage))
    {
      GNUNET_break (0);
      reschedule_connect (h);
      return;
    }
    rcm = (const struct GNUNET_IDENTITY_ResultCodeMessage *) msg;
    str = (const char *) &rcm[1];
    if ( (size > sizeof (struct GNUNET_IDENTITY_ResultCodeMessage)) &&
	 ('\0' != str[size - sizeof (struct GNUNET_IDENTITY_ResultCodeMessage) - 1]) )
    {
      GNUNET_break (0);
      reschedule_connect (h);
      return;
    }
    if (size == sizeof (struct GNUNET_IDENTITY_ResultCodeMessage))
      str = NULL;

    op = h->op_head;
    GNUNET_CONTAINER_DLL_remove (h->op_head,
				 h->op_tail,
				 op);
    if (NULL != op->cont)
      op->cont (op->cls,
		str);
    GNUNET_break (NULL == op->cb);
    GNUNET_free (op);
    break;
  case GNUNET_MESSAGE_TYPE_IDENTITY_UPDATE:
    if (size < sizeof (struct GNUNET_IDENTITY_UpdateMessage))
    {
      GNUNET_break (0);
      reschedule_connect (h);
      return;
    }
    um = (const struct GNUNET_IDENTITY_UpdateMessage *) msg;
    pk_len = ntohs (um->pk_len);
    name_len = ntohs (um->name_len);    
    str = (const char *) &um[1];
    if ( (size != pk_len + name_len + sizeof (struct GNUNET_IDENTITY_UpdateMessage)) ||
	 ( (0 != name_len) &&
	   ('\0' != str[pk_len + name_len - 1])) )
    {
      GNUNET_break (0);
      reschedule_connect (h);
      return;
    }
    priv = GNUNET_CRYPTO_ecc_decode_key (str, pk_len, GNUNET_YES); 
    if (NULL == priv)
    {
      GNUNET_break (0);
      reschedule_connect (h);
      return;
    }
    GNUNET_CRYPTO_ecc_key_get_public (priv,
				      &pub);
    GNUNET_CRYPTO_hash (&pub, sizeof (pub), &id);
    if (0 == name_len)
      str = NULL;
    else
      str = &str[pk_len];
    ego = GNUNET_CONTAINER_multihashmap_get (h->egos,
					     &id);
    if (NULL == ego)
    {
      /* ego was created */
      if (NULL == str)
      {
	/* deletion of unknown ego? not allowed */
	GNUNET_break (0);
	GNUNET_CRYPTO_ecc_key_free (priv);
	reschedule_connect (h);
	return;
      }
      ego = GNUNET_new (struct GNUNET_IDENTITY_Ego);
      ego->pk = priv;
      ego->identifier = GNUNET_strdup (str);
      ego->id = id;
      GNUNET_assert (GNUNET_YES ==
		     GNUNET_CONTAINER_multihashmap_put (h->egos,
							&ego->id,
							ego,
							GNUNET_CONTAINER_MULTIHASHMAPOPTION_UNIQUE_ONLY));
    }
    else
    {
      GNUNET_CRYPTO_ecc_key_free (priv);
    }
    /* inform application about change */
    h->cb (h->cb_cls,
	   ego,
	   &ego->ctx,
	   str);
    if (NULL == str)
    {
      /* ego was deleted */
      GNUNET_assert (GNUNET_YES ==
		     GNUNET_CONTAINER_multihashmap_remove (h->egos,
							   &ego->id,
							   ego));
      GNUNET_CRYPTO_ecc_key_free (ego->pk);
      GNUNET_free (ego->identifier);
      GNUNET_free (ego);
    }
    else
      {
      /* ego changed name */
      GNUNET_free (ego->identifier);
      ego->identifier = GNUNET_strdup (str);
    }    
    break;
  case GNUNET_MESSAGE_TYPE_IDENTITY_SET_DEFAULT:
    if (size < sizeof (struct GNUNET_IDENTITY_SetDefaultMessage))
    {
      GNUNET_break (0);
      reschedule_connect (h);
      return;
    }
    sdm = (const struct GNUNET_IDENTITY_SetDefaultMessage *) msg;
    pk_len = ntohs (sdm->pk_len);
    name_len = ntohs (sdm->name_len);
    str = (const char *) &sdm[1];
    if ( (size != pk_len + name_len + sizeof (struct GNUNET_IDENTITY_SetDefaultMessage)) ||
	 ( (0 != name_len) &&
	   ('\0' != str[pk_len + name_len - 1]) ) )
    {
      GNUNET_break (0);
      reschedule_connect (h);
      return;
    }
    priv = GNUNET_CRYPTO_ecc_decode_key (str, pk_len, GNUNET_YES); 
    if (NULL == priv)
    {
      GNUNET_break (0);
      reschedule_connect (h);
      return;
    }
    GNUNET_CRYPTO_ecc_key_get_public (priv,
				      &pub);
    GNUNET_CRYPTO_ecc_key_free (priv);
    GNUNET_CRYPTO_hash (&pub, sizeof (pub), &id);
    if (0 == name_len)
      str = NULL;
    else
      str = &str[pk_len];
    ego = GNUNET_CONTAINER_multihashmap_get (h->egos,
					     &id);
    if (NULL == ego)
    {
      GNUNET_break (0);
      reschedule_connect (h);
      return;
    }
    op = h->op_head;
    GNUNET_CONTAINER_DLL_remove (h->op_head,
				 h->op_tail,
				 op);
    if (NULL != op->cb)
      op->cb (op->cls,
	      ego,
	      &ego->ctx,
	      ego->identifier);
    GNUNET_break (NULL == op->cont);
    GNUNET_free (op);
    break;
  default:
    GNUNET_break (0);
    reschedule_connect (h);
    return;
  }
  GNUNET_CLIENT_receive (h->client, &message_handler, h,
                         GNUNET_TIME_UNIT_FOREVER_REL);
}


/**
 * Schedule transmission of the next message from our queue.
 *
 * @param h identity handle
 */
static void
transmit_next (struct GNUNET_IDENTITY_Handle *h);


/**
 * Transmit next message to service.
 *
 * @param cls the 'struct GNUNET_IDENTITY_Handle'.
 * @param size number of bytes available in buf
 * @param buf where to copy the message
 * @return number of bytes copied to buf
 */
static size_t
send_next_message (void *cls, 
		   size_t size, 
		   void *buf)
{
  struct GNUNET_IDENTITY_Handle *h = cls;
  struct GNUNET_IDENTITY_Operation *op = h->op_head;
  size_t ret;
  
  h->th = NULL;
  if (NULL == op)
    return 0;
  ret = ntohs (op->msg->size);
  if (ret > size)
  {
    reschedule_connect (h);
    return 0;
  }  
  memcpy (buf, op->msg, ret);
  if ( (NULL == op->cont) &&
       (NULL == op->cb) )
  {
    GNUNET_CONTAINER_DLL_remove (h->op_head,
				 h->op_tail,
				 op);
    GNUNET_free (op);
    transmit_next (h);
  }
  if (GNUNET_NO == h->in_receive)
  {
    h->in_receive = GNUNET_YES;
    GNUNET_CLIENT_receive (h->client,
			   &message_handler, h,
			   GNUNET_TIME_UNIT_FOREVER_REL);
  }
  return ret;
}


/**
 * Schedule transmission of the next message from our queue.
 *
 * @param h identity handle
 */
static void
transmit_next (struct GNUNET_IDENTITY_Handle *h)
{
  struct GNUNET_IDENTITY_Operation *op = h->op_head;

  GNUNET_assert (NULL == h->th);
  if (NULL == op)
    return;
  h->th = GNUNET_CLIENT_notify_transmit_ready (h->client,
					       ntohs (op->msg->size),
					       GNUNET_TIME_UNIT_FOREVER_REL,
					       GNUNET_NO,
					       &send_next_message,
					       h);
}


/**
 * Try again to connect to network size estimation service.
 *
 * @param cls the handle to the transport service
 * @param tc scheduler context
 */
static void
reconnect (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct GNUNET_IDENTITY_Handle *h = cls;
  struct GNUNET_IDENTITY_Operation *op;
  struct GNUNET_MessageHeader msg;

  h->reconnect_task = GNUNET_SCHEDULER_NO_TASK;
  LOG (GNUNET_ERROR_TYPE_DEBUG,
       "Connecting to identity service.\n");
  GNUNET_assert (NULL == h->client);
  h->client = GNUNET_CLIENT_connect ("identity", h->cfg);
  GNUNET_assert (NULL != h->client);
  op = GNUNET_malloc (sizeof (struct GNUNET_IDENTITY_Operation) + 
		      sizeof (struct GNUNET_MessageHeader));
  op->h = h;
  op->msg = (const struct GNUNET_MessageHeader *) &op[1];
  msg.size = htons (sizeof (msg));
  msg.type = htons (GNUNET_MESSAGE_TYPE_IDENTITY_START);
  memcpy (&op[1], &msg, sizeof (msg));
  GNUNET_CONTAINER_DLL_insert (h->op_head,
			       h->op_tail,
			       op);
  transmit_next (h);
  GNUNET_assert (NULL != h->th);
}


/**
 * Connect to the identity service.
 *
 * @param cfg the configuration to use
 * @param cb function to call on all identity events, can be NULL
 * @param cb_cls closure for 'cb'
 * @return handle to use
 */
struct GNUNET_IDENTITY_Handle *
GNUNET_IDENTITY_connect (const struct GNUNET_CONFIGURATION_Handle *cfg,
			 GNUNET_IDENTITY_Callback cb,
			 void *cb_cls)
{
  struct GNUNET_IDENTITY_Handle *h;

  h = GNUNET_malloc (sizeof (struct GNUNET_IDENTITY_Handle));
  h->cfg = cfg;
  h->cb = cb;
  h->cb_cls = cb_cls;
  h->reconnect_delay = GNUNET_TIME_UNIT_ZERO;
  h->reconnect_task = GNUNET_SCHEDULER_add_now (&reconnect, h);
  return h;
}


/**
 * Obtain the ECC key associated with a ego.
 *
 * @param ego the ego
 * @return associated ECC key, valid as long as the ego is valid
 */
const struct GNUNET_CRYPTO_EccPrivateKey *
GNUNET_IDENTITY_ego_get_key (struct GNUNET_IDENTITY_Ego *ego)
{
  return ego->pk;
}


/**
 * Obtain the identity that is currently preferred/default
 * for a service.
 *
 * @param id identity service to query
 * @param service_name for which service is an identity wanted
 * @param cb function to call with the result (will only be called once)
 * @param cb_cls closure for cb
 * @return handle to abort the operation
 */
struct GNUNET_IDENTITY_Operation *
GNUNET_IDENTITY_get (struct GNUNET_IDENTITY_Handle *id,
		     const char *service_name,
		     GNUNET_IDENTITY_Callback cb,
		     void *cb_cls)
{
  struct GNUNET_IDENTITY_Operation *op;
  struct GNUNET_IDENTITY_GetDefaultMessage *gdm;
  size_t slen;

  slen = strlen (service_name) + 1; 
  if (slen >= GNUNET_SERVER_MAX_MESSAGE_SIZE - sizeof (struct GNUNET_IDENTITY_GetDefaultMessage))
  {
    GNUNET_break (0);
    return NULL;
  }
  op = GNUNET_malloc (sizeof (struct GNUNET_IDENTITY_Operation) +
		      sizeof (struct GNUNET_IDENTITY_GetDefaultMessage) +
		      slen);  
  op->cb = cb;
  op->cls = cb_cls;
  gdm = (struct GNUNET_IDENTITY_GetDefaultMessage *) &op[1];
  gdm->header.type = htons (GNUNET_MESSAGE_TYPE_IDENTITY_GET_DEFAULT);
  gdm->header.size = htons (sizeof (struct GNUNET_IDENTITY_GetDefaultMessage) +
			    slen);
  gdm->name_len = htons (slen);
  gdm->reserved = htons (0);
  memcpy (&gdm[1], service_name, slen);
  op->msg = &gdm->header;
  GNUNET_CONTAINER_DLL_insert_tail (id->op_head,
				    id->op_tail,
				    op);
  if (NULL == id->th)
    transmit_next (id);
  return op;
}


/**
 * Set the preferred/default identity for a service.
 *
 * @param id identity service to inform
 * @param service_name for which service is an identity set
 * @param ego new default identity to be set for this service
 * @param cont function to call once the operation finished
 * @param cont_cls closure for cont
 * @return handle to abort the operation
 */
struct GNUNET_IDENTITY_Operation *
GNUNET_IDENTITY_set (struct GNUNET_IDENTITY_Handle *id,
		     const char *service_name,
		     struct GNUNET_IDENTITY_Ego *ego,
		     GNUNET_IDENTITY_Continuation cont,
		     void *cont_cls)
{
  GNUNET_break (0); // FIXME
  return NULL;
}


/** 
 * Create a new identity with the given identifier.
 *
 * @param id identity service to use
 * @param identifier desired identifier
 * @param cb function to call with the result (will only be called once)
 * @param cb_cls closure for cb
 * @return handle to abort the operation
 */
struct GNUNET_IDENTITY_Operation *
GNUNET_IDENTITY_create (struct GNUNET_IDENTITY_Handle *id,
			const char *identifier,
			GNUNET_IDENTITY_Callback cb,
			void *cb_cls)
{
  GNUNET_break (0); // FIXME
  return NULL;
}


/** 
 * Renames an existing identity.
 *
 * @param id identity service to use
 * @param old_identifier old identifier
 * @param new_identifier desired new identifier
 * @param cb function to call with the result (will only be called once)
 * @param cb_cls closure for cb
 * @return handle to abort the operation
 */
struct GNUNET_IDENTITY_Operation *
GNUNET_IDENTITY_rename (struct GNUNET_IDENTITY_Handle *id,
			const char *old_identifier,
			const char *new_identifier,
			GNUNET_IDENTITY_Continuation cb,
			void *cb_cls)
{
  struct GNUNET_IDENTITY_Operation *op;
  struct GNUNET_IDENTITY_RenameMessage *grm;
  size_t slen_old;
  size_t slen_new;
  char *dst;

  slen_old = strlen (old_identifier) + 1;
  slen_new = strlen (new_identifier) + 1;
  if ( (slen_old >= GNUNET_SERVER_MAX_MESSAGE_SIZE) ||
       (slen_new >= GNUNET_SERVER_MAX_MESSAGE_SIZE) ||
       (slen_old + slen_new >= GNUNET_SERVER_MAX_MESSAGE_SIZE - sizeof (struct GNUNET_IDENTITY_RenameMessage)) )
  {
    GNUNET_break (0);
    return NULL;
  }
  op = GNUNET_malloc (sizeof (struct GNUNET_IDENTITY_Operation) +
		      sizeof (struct GNUNET_IDENTITY_RenameMessage) +
		      slen_old + slen_new);
  op->cont = cb;
  op->cls = cb_cls;
  grm = (struct GNUNET_IDENTITY_RenameMessage *) &op[1];
  grm->header.type = htons (GNUNET_MESSAGE_TYPE_IDENTITY_RENAME);
  grm->header.size = htons (sizeof (struct GNUNET_IDENTITY_RenameMessage) +
			    slen_old + slen_new);
  grm->old_name_len = htons (slen_old);
  grm->new_name_len = htons (slen_new);
  dst = (char *) &grm[1];
  memcpy (dst, old_identifier, slen_old);
  memcpy (&dst[slen_old], new_identifier, slen_new);
  op->msg = &grm->header;
  GNUNET_CONTAINER_DLL_insert_tail (id->op_head,
				    id->op_tail,
				    op);
  if (NULL == id->th)
    transmit_next (id);
  return op;
}


/** 
 * Delete an existing identity.
 *
 * @param id identity service to use
 * @param identifier identifier of the identity to delete
 * @param cb function to call with the result (will only be called once)
 * @param cb_cls closure for cb
 * @return handle to abort the operation
 */
struct GNUNET_IDENTITY_Operation *
GNUNET_IDENTITY_delete (struct GNUNET_IDENTITY_Handle *id,
			const char *identifier,
			GNUNET_IDENTITY_Continuation cb,
			void *cb_cls)
{
  struct GNUNET_IDENTITY_Operation *op;
  struct GNUNET_IDENTITY_DeleteMessage *gdm;
  size_t slen;

  slen = strlen (identifier) + 1;
  if (slen >= GNUNET_SERVER_MAX_MESSAGE_SIZE - sizeof (struct GNUNET_IDENTITY_DeleteMessage))
  {
    GNUNET_break (0);
    return NULL;
  }
  op = GNUNET_malloc (sizeof (struct GNUNET_IDENTITY_Operation) +
		      sizeof (struct GNUNET_IDENTITY_DeleteMessage) +
		      slen);  
  op->cont = cb;
  op->cls = cb_cls;
  gdm = (struct GNUNET_IDENTITY_DeleteMessage *) &op[1];
  gdm->header.type = htons (GNUNET_MESSAGE_TYPE_IDENTITY_DELETE);
  gdm->header.size = htons (sizeof (struct GNUNET_IDENTITY_DeleteMessage) +
			    slen);
  gdm->name_len = htons (slen);
  gdm->reserved = htons (0);
  memcpy (&gdm[1], identifier, slen);
  op->msg = &gdm->header;
  GNUNET_CONTAINER_DLL_insert_tail (id->op_head,
				    id->op_tail,
				    op);
  if (NULL == id->th)
    transmit_next (id);
  return op;
}


/**
 * Cancel an identity operation. Note that the operation MAY still
 * be executed; this merely cancels the continuation; if the request
 * was already transmitted, the service may still choose to complete
 * the operation.
 *
 * @param op operation to cancel
 */
void
GNUNET_IDENITY_cancel (struct GNUNET_IDENTITY_Operation *op)
{
  struct GNUNET_IDENTITY_Handle *h = op->h;

  if ( (h->op_head != op) ||
       (NULL == h->client) )
  {
    /* request not active, can simply remove */
    GNUNET_CONTAINER_DLL_remove (h->op_head,
				 h->op_tail,
				 op);
    GNUNET_free (op);
    return;
  }
  if (NULL != h->th)
  {
    /* request active but not yet with service, can still abort */
    GNUNET_CLIENT_notify_transmit_ready_cancel (h->th);
    h->th = NULL;
    GNUNET_CONTAINER_DLL_remove (h->op_head,
				 h->op_tail,
				 op);
    GNUNET_free (op);
    transmit_next (h);
    return;
  }
  /* request active with service, simply ensure continuations are not called */
  op->cont = NULL;
  op->cb = NULL;
}


/**
 * Disconnect from identity service
 *
 * @param h handle to destroy
 */
void
GNUNET_IDENTITY_disconnect (struct GNUNET_IDENTITY_Handle *h)
{
  GNUNET_assert (NULL != h);
  if (h->reconnect_task != GNUNET_SCHEDULER_NO_TASK)
  {
    GNUNET_SCHEDULER_cancel (h->reconnect_task);
    h->reconnect_task = GNUNET_SCHEDULER_NO_TASK;
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
  GNUNET_free (h);
}

/* end of identity_api.c */
