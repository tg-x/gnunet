/*
     This file is part of GNUnet.
     (C) 2011-2013 Christian Grothoff (and other contributing authors)

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
 * @file gns/gnunet-service-gns_resolver.c
 * @brief GNUnet GNS resolver logic
 * @author Martin Schanzenbach
 * @author Christian Grothoff
 */
#include "platform.h"
#include "gnunet_util_lib.h"
#include "gnunet_transport_service.h"
#include "gnunet_dnsstub_lib.h"
#include "gnunet_dht_service.h"
#include "gnunet_namestore_service.h"
#include "gnunet_dns_service.h"
#include "gnunet_resolver_service.h"
#include "gnunet_dnsparser_lib.h"
#include "gns_protocol.h"
#include "gnunet_gns_service.h"
#include "gns_common.h"
#include "gns.h"
#include "gnunet-service-gns_resolver.h"
#include "gnunet_vpn_service.h"


/**
 * Default DHT timeout for lookups.
 */
#define DHT_LOOKUP_TIMEOUT GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS, 60)

/**
 * DHT replication level
 */
#define DHT_GNS_REPLICATION_LEVEL 5


/**
 * DLL to hold the authority chain we had to pass in the resolution
 * process.
 */
struct AuthorityChain
{
  /**
   * This is a DLL.
   */
  struct AuthorityChain *prev;

  /**
   * This is a DLL.
   */
  struct AuthorityChain *next;

  /**
   * label corresponding to the authority 
   */
  char label[GNUNET_DNSPARSER_MAX_LABEL_LENGTH];
  
  /**
   * #GNUNET_YES if the authority was a GNS authority,
   * #GNUNET_NO if the authority was a DNS authority.
   */
  int gns_authority;

  /**
   * Information about the resolver authority for this label.
   */
  union
  {

    /**
     * The zone of the GNS authority 
     */
    struct GNUNET_CRYPTO_EccPublicKey gns_authority;

    struct
    {
      /**
       * Domain of the DNS resolver that is the authority.
       * (appended to construct the DNS name to resolve;
       * this is NOT the DNS name of the DNS server!).
       */
      char name[GNUNET_DNSPARSER_MAX_NAME_LENGTH];

      /**
       * IP address of the DNS resolver that is authoritative.
       * (this implementation currently only supports one
       * IP at a time).
       */
      struct sockaddr_storage dns_ip;

    } dns_authority;

  } authority_info;
  
};


/**
 * Resolution status indicator
 */
enum ResolutionStatus
{
  /**
   * the name to lookup exists
   */
  RSL_RECORD_EXISTS = 1,

  /**
   * the name in the record expired
   */
  RSL_RECORD_EXPIRED = 2,
 
  /**
   * resolution timed out
   */
  RSL_TIMED_OUT = 4,
 
  /**
   * Found VPN delegation
   */
  RSL_DELEGATE_VPN = 8,
 
  /**
   * Found NS delegation
   */
  RSL_DELEGATE_NS = 16,
 
  /**
   * Found PKEY delegation
   */
  RSL_DELEGATE_PKEY = 32,
  
  /**
   * Found CNAME record
   */
  RSL_CNAME_FOUND = 64,
  
  /**
   * Found PKEY has been revoked
   */
  RSL_PKEY_REVOKED = 128
};


/**
 * Handle to a currenty pending resolution.  On result (positive or
 * negative) the #GNS_ResultProcessor is called.  
 */
struct GNS_ResolverHandle
{

  /**
   * DLL 
   */
  struct GNS_ResolverHandle *next;

  /**
   * DLL 
   */
  struct GNS_ResolverHandle *prev;

  /**
   * The top-level GNS authoritative zone to query 
   */
  struct GNUNET_CRYPTO_EccPublicKey authority_zone;

  /**
   * called when resolution phase finishes 
   */
  GNS_ResultProcessor proc;
  
  /**
   * closure passed to proc 
   */
  void* proc_cls;

  /**
   * Handle for DHT lookups. should be NULL if no lookups are in progress 
   */
  struct GNUNET_DHT_GetHandle *get_handle;

  /**
   * Handle to a VPN request, NULL if none is active.
   */
  struct GNUNET_VPN_RedirectionRequest *vpn_handle;

  /**
   * Socket for a DNS request, NULL if none is active.
   */
  struct GNUNET_DNSSTUB_RequestSocket *dns_request;

  /**
   * Pending Namestore task
   */
  struct GNUNET_NAMESTORE_QueueEntry *namestore_task;

  /**
   * Heap node associated with this lookup.  Used to limit number of
   * concurrent requests.
   */
  struct GNUNET_CONTAINER_HeapNode *dht_heap_node;

  /**
   * DLL to store the authority chain 
   */
  struct AuthorityChain *authority_chain_head;

  /**
   * DLL to store the authority chain 
   */
  struct AuthorityChain *authority_chain_tail;

  /**
   * Private key of the shorten zone, NULL to not shorten.
   */
  struct GNUNET_CRYPTO_EccPrivateKey *shorten_key;

  /**
   * The name to resolve 
   */
  char name[GNUNET_DNSPARSER_MAX_NAME_LENGTH];

  /**
   * Current offset in 'name' where we are resolving.
   */
  size_t name_resolution_pos;

  /**
   * Use only cache 
   */
  int only_cached;

};


/**
 * Handle for a PSEU lookup used to shorten names.
 */
struct GetPseuAuthorityHandle
{
  /**
   * DLL
   */
  struct GetPseuAuthorityHandle *next;

  /**
   * DLL
   */
  struct GetPseuAuthorityHandle *prev;

  /**
   * Private key of the (shorten) zone to store the resulting
   * pseudonym in.
   */
  struct GNUNET_CRYPTO_EccPrivateKey shorten_zone_key;

  /**
   * Original label (used if no PSEU record is found).
   */
  char label[GNUNET_DNSPARSER_MAX_LABEL_LENGTH + 1];

  /**
   * The zone for which we are trying to find the PSEU record.
   */
  struct GNUNET_CRYPTO_EccPublicKey target_zone;

  /**
   * Handle for DHT lookups. Should be NULL if no lookups are in progress 
   */
  struct GNUNET_DHT_GetHandle *get_handle;

  /**
   * Handle to namestore request 
   */
  struct GNUNET_NAMESTORE_QueueEntry *namestore_task;

  /**
   * Task to abort DHT lookup operation.
   */
  GNUNET_SCHEDULER_TaskIdentifier timeout_task;

};


/**
 * Our handle to the namestore service
 */
static struct GNUNET_NAMESTORE_Handle *namestore_handle;

/**
 * Our handle to the vpn service
 */
static struct GNUNET_VPN_Handle *vpn_handle;

/**
 * Resolver handle to the dht
 */
static struct GNUNET_DHT_Handle *dht_handle;

/**
 * Handle to perform DNS lookups.
 */
static struct GNUNET_DNSSTUB_Context *dns_handle;

/**
 * Heap for limiting parallel DHT lookups
 */
static struct GNUNET_CONTAINER_Heap *dht_lookup_heap;

/**
 * Maximum amount of parallel queries in background
 */
static unsigned long long max_allowed_background_queries;

/**
 * Head of PSEU/shorten operations list.
 */
struct GetPseuAuthorityHandle *gph_head;

/**
 * Tail of PSEU/shorten operations list.
 */
struct GetPseuAuthorityHandle *gph_tail;

/**
 * Head of resolver lookup list
 */
static struct GNS_ResolverHandle *rlh_head;

/**
 * Tail of resolver lookup list
 */
static struct GNS_ResolverHandle *rlh_tail;

/**
 * Global configuration.
 */
static const struct GNUNET_CONFIGURATION_Handle *cfg;


/**
 * Check if name is in srv format (_x._y.xxx)
 *
 * @param name
 * @return GNUNET_YES if true
 */
static int
is_srv (const char *name)
{
  char *ndup;
  int ret;

  if (*name != '_')
    return GNUNET_NO;
  if (NULL == strstr (name, "._"))
    return GNUNET_NO;
  ret = GNUNET_YES;
  ndup = GNUNET_strdup (name);
  strtok (ndup, ".");
  if (NULL == strtok (NULL, "."))
    ret = GNUNET_NO;
  if (NULL == strtok (NULL, "."))
    ret = GNUNET_NO;
  if (NULL != strtok (NULL, "."))
    ret = GNUNET_NO;
  GNUNET_free (ndup);
  return ret;
}


/**
 * Determine if this name is canonical (is a legal name in a zone, without delegation);
 * note that we do not test that the name does not contain illegal characters, we only
 * test for delegation.  Note that service records (i.e. _foo._srv) are canonical names
 * even though they consist of multiple labels.
 *
 * Examples:
 * a.b.gads  = not canonical
 * a         = canonical
 * _foo._srv = canonical
 * _f.bar    = not canonical
 *
 * @param name the name to test
 * @return GNUNET_YES if canonical
 */
static int
is_canonical (const char *name)
{
  const char *pos;
  const char *dot;

  if (NULL == strchr (name, '.'))
    return GNUNET_YES;
  if ('_' != name[0])
    return GNUNET_NO;
  pos = &name[1];
  while (NULL != (dot = strchr (pos, '.')))    
    if ('_' != dot[1])
      return GNUNET_NO;
    else
      pos = dot + 1;
  return GNUNET_YES;
}


/* ******************** Shortening logic ************************ */


/**
 * Cleanup a 'struct GetPseuAuthorityHandle', terminating all
 * pending activities.
 *
 * @param gph handle to terminate
 */
static void
free_get_pseu_authority_handle (struct GetPseuAuthorityHandle *gph)
{
  if (NULL != gph->get_handle)
  {
    GNUNET_DHT_get_stop (gph->get_handle);
    gph->get_handle = NULL;
  }
  if (NULL != gph->namestore_task)
  {
    GNUNET_NAMESTORE_cancel (gph->namestore_task);
    gph->namestore_task = NULL;
  }
  if (GNUNET_SCHEDULER_NO_TASK != gph->timeout_task)
  {
    GNUNET_SCHEDULER_cancel (gph->timeout_task);
    gph->timeout_task = GNUNET_SCHEDULER_NO_TASK;
  }
  GNUNET_CONTAINER_DLL_remove (gph_head, gph_tail, gph);
  GNUNET_free (gph);
}


/**
 * Continuation for pkey record creation (shorten)
 *
 * @param cls a GetPseuAuthorityHandle
 * @param success unused
 * @param emsg unused
 */
static void
create_pkey_cont (void* cls, 
		  int32_t success, 
		  const char *emsg)
{
  struct GetPseuAuthorityHandle* gph = cls;

  gph->namestore_task = NULL;
  free_get_pseu_authority_handle (gph);
}


/**
 * Namestore calls this function if we have record for this name.
 * (or with rd_count=0 to indicate no matches).
 *
 * @param cls the pending query
 * @param key the key of the zone we did the lookup
 * @param name the name for which we need an authority
 * @param rd_count the number of records with 'name'
 * @param rd the record data
 */
static void
process_pseu_lookup_ns (void *cls,
			const struct GNUNET_CRYPTO_EccPrivateKey *key,
			const char *name, 
			unsigned int rd_count,
			const struct GNUNET_NAMESTORE_RecordData *rd)
{
  struct GetPseuAuthorityHandle *gph = cls;
  struct GNUNET_NAMESTORE_RecordData new_pkey;

  gph->namestore_task = NULL;
  if (rd_count > 0)
  {
    GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
               "Name `%s' already taken, cannot shorten.\n", 
	       name);
    /* if this was not yet the original label, try one more
       time, this time not using PSEU but the original label */
    if (0 == strcmp (name,
		     gph->label))
      free_get_pseu_authority_handle (gph);
    else
      gph->namestore_task = GNUNET_NAMESTORE_lookup (namestore_handle,
						     &gph->shorten_zone_key,
						     gph->label,
						     GNUNET_NAMESTORE_TYPE_ANY,
						     &process_pseu_lookup_ns,
						     gph);
    return;
  }
  /* name is available */
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
	      "Shortening `%s' to `%s'\n", 
	      GNUNET_NAMESTORE_z2s (&gph->target_zone),
	      name);
  new_pkey.expiration_time = UINT64_MAX;
  new_pkey.data_size = sizeof (struct GNUNET_CRYPTO_EccPublicKey);
  new_pkey.data = &gph->target_zone;
  new_pkey.record_type = GNUNET_NAMESTORE_TYPE_PKEY;
  new_pkey.flags = GNUNET_NAMESTORE_RF_AUTHORITY
                 | GNUNET_NAMESTORE_RF_PRIVATE
                 | GNUNET_NAMESTORE_RF_PENDING;
  gph->namestore_task 
    = GNUNET_NAMESTORE_records_store (namestore_handle,
				      &gph->shorten_zone_key,
				      name,
				      1, &new_pkey,
				      &create_pkey_cont, gph);
}


/**
 * Process result of a DHT lookup for a PSEU record.
 *
 * @param gph the handle to our shorten operation
 * @param pseu the pseu result or NULL
 */
static void
process_pseu_result (struct GetPseuAuthorityHandle* gph, 
		     const char *pseu)
{
  if (NULL == pseu)
  {
    /* no PSEU found, try original label */
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "No PSEU found, trying original label `%s' instead.\n",
		gph->label);
    gph->namestore_task = GNUNET_NAMESTORE_lookup (namestore_handle,
						   &gph->shorten_zone_key,
						   gph->label,
						   GNUNET_NAMESTORE_TYPE_ANY,
						   &process_pseu_lookup_ns,
						   gph);
    return;
  }
  
  /* check if 'pseu' is taken */
  gph->namestore_task = GNUNET_NAMESTORE_lookup (namestore_handle,
						 &gph->shorten_zone_key,
						 pseu,
						 GNUNET_NAMESTORE_TYPE_ANY,
						 &process_pseu_lookup_ns,
						 gph);
}


/**
 * Handle timeout for DHT request during shortening.
 *
 * @param cls the request handle as closure
 * @param tc the task context
 */
static void
handle_auth_discovery_timeout (void *cls,
                               const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct GetPseuAuthorityHandle *gph = cls;

  gph->timeout_task = GNUNET_SCHEDULER_NO_TASK;
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "DHT lookup for PSEU query timed out.\n");
  GNUNET_DHT_get_stop (gph->get_handle);
  gph->get_handle = NULL;
  process_pseu_result (gph, NULL);
}


/**
 * Handle decrypted records from DHT result.
 *
 * @param cls closure with our 'struct GetPseuAuthorityHandle'
 * @param rd_count number of entries in 'rd' array
 * @param rd array of records with data to store
 */
static void
process_auth_records (void *cls,
		      unsigned int rd_count,
		      const struct GNUNET_NAMESTORE_RecordData *rd)
{
  struct GetPseuAuthorityHandle *gph = cls;
  unsigned int i;

  for (i=0; i < rd_count; i++)
  {
    if (GNUNET_NAMESTORE_TYPE_PSEU == rd[i].record_type)
    {
      /* found pseu */
      process_pseu_result (gph, 
			   (const char *) rd[i].data);
      return;
    }
  }
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "No PSEU record found in DHT reply.\n");
  process_pseu_result (gph, NULL);
}


/**
 * Function called when we find a PSEU entry in the DHT
 *
 * @param cls the request handle
 * @param exp lifetime
 * @param key the key the record was stored under
 * @param get_path get path
 * @param get_path_length get path length
 * @param put_path put path
 * @param put_path_length put path length
 * @param type the block type
 * @param size the size of the record
 * @param data the record data
 */
static void
process_auth_discovery_dht_result (void* cls,
                                   struct GNUNET_TIME_Absolute exp,
                                   const struct GNUNET_HashCode *key,
                                   const struct GNUNET_PeerIdentity *get_path,
                                   unsigned int get_path_length,
                                   const struct GNUNET_PeerIdentity *put_path,
                                   unsigned int put_path_length,
                                   enum GNUNET_BLOCK_Type type,
                                   size_t size,
                                   const void *data)
{
  struct GetPseuAuthorityHandle *gph = cls;
  const struct GNUNET_NAMESTORE_Block *block;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Got DHT result for PSEU request\n");
  GNUNET_DHT_get_stop (gph->get_handle);
  gph->get_handle = NULL;
  GNUNET_SCHEDULER_cancel (gph->timeout_task);
  gph->timeout_task = GNUNET_SCHEDULER_NO_TASK;

  if (NULL == data)
  {
    /* is this allowed!? */
    GNUNET_break (0);
    process_pseu_result (gph, NULL);
    return;
  }
  if (size < sizeof (struct GNUNET_NAMESTORE_Block))
  {
    /* how did this pass DHT block validation!? */
    GNUNET_break (0);
    process_pseu_result (gph, NULL);
    return;   
  }
  block = data;
  if (size !=
      ntohs (block->purpose.size) + 
      sizeof (struct GNUNET_CRYPTO_EccPublicKey) +
      sizeof (struct GNUNET_CRYPTO_EccSignature))
  {
    /* how did this pass DHT block validation!? */
    GNUNET_break (0);
    process_pseu_result (gph, NULL);
    return;   
  }
  if (GNUNET_OK !=
      GNUNET_NAMESTORE_block_decrypt (block,
				      &gph->target_zone,
				      GNUNET_GNS_TLD_PLUS,
				      &process_auth_records,
				      gph))
  {
    /* other peer encrypted invalid block, complain */
    GNUNET_break_op (0);
    process_pseu_result (gph, NULL);
    return;   
  }
}


/**
 * Callback called by namestore for a zone to name result.  We're
 * trying to see if a short name for a given zone already exists.
 *
 * @param cls the closure
 * @param zone_key the zone we queried
 * @param name the name found or NULL
 * @param rd_len number of records for the name
 * @param rd the record data (PKEY) for the name
 */
static void
process_zone_to_name_discover (void *cls,
			       const struct GNUNET_CRYPTO_EccPrivateKey *zone_key,
			       const char *name,
			       unsigned int rd_len,
			       const struct GNUNET_NAMESTORE_RecordData *rd)
{
  struct GetPseuAuthorityHandle* gph = cls;
  struct GNUNET_HashCode lookup_key;
  
  gph->namestore_task = NULL;
  if (0 != rd_len)
  {
    /* we found a match in our own zone */
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		"Shortening aborted, name `%s' already reserved for the zone\n",
		name);
    free_get_pseu_authority_handle (gph);
    return;
  }
  /* record does not yet exist, go into DHT to find PSEU record */
  GNUNET_NAMESTORE_query_from_public_key (&gph->target_zone,
					  GNUNET_GNS_TLD_PLUS, 					  
					  &lookup_key);
  gph->timeout_task = GNUNET_SCHEDULER_add_delayed (DHT_LOOKUP_TIMEOUT,
						    &handle_auth_discovery_timeout, 
						    gph);
  gph->get_handle = GNUNET_DHT_get_start (dht_handle,
					  GNUNET_BLOCK_TYPE_GNS_NAMERECORD,
					  &lookup_key,
					  DHT_GNS_REPLICATION_LEVEL,
					  GNUNET_DHT_RO_DEMULTIPLEX_EVERYWHERE,
					  NULL, 0,
					  &process_auth_discovery_dht_result,
					  gph);
}


/**
 * Start shortening algorithm, try to allocate a nice short
 * canonical name for @a pub in @a shorten_zone, using
 * @a original_label as one possible suggestion.
 *
 * @param original_label original label for the zone
 * @param pub public key of the zone to shorten
 * @param shorten_zone private key of the target zone for the new record
 */
static void
start_shorten (const char *original_label,
	       const struct GNUNET_CRYPTO_EccPublicKey *pub,
               const struct GNUNET_CRYPTO_EccPrivateKey *shorten_zone)
{
  struct GetPseuAuthorityHandle *gph;
  
  if (strlen (original_label) > GNUNET_DNSPARSER_MAX_LABEL_LENGTH)
  {
    GNUNET_break (0);
    return;
  }
  gph = GNUNET_new (struct GetPseuAuthorityHandle);
  gph->shorten_zone_key = *shorten_zone;
  gph->target_zone = *pub;
  strcpy (gph->label, original_label);
  GNUNET_CONTAINER_DLL_insert (gph_head, gph_tail, gph);
  /* first, check if we *already* have a record for this zone */
  gph->namestore_task = GNUNET_NAMESTORE_zone_to_name (namestore_handle,
                                                       shorten_zone,
                                                       pub,
                                                       &process_zone_to_name_discover,
                                                       gph);
}


/* ************************** Resolution **************************** */

#if 0
/**
 * Helper function to free resolver handle
 *
 * @param rh the handle to free
 */
static void
free_resolver_handle (struct ResolverHandle* rh)
{
  struct AuthorityChain *ac;
  struct AuthorityChain *ac_next;

  if (NULL == rh)
    return;

  ac_next = rh->authority_chain_head;
  while (NULL != (ac = ac_next))
  {
    ac_next = ac->next;
    GNUNET_free (ac);
  }
  
  if (NULL != rh->get_handle)
    GNUNET_DHT_get_stop (rh->get_handle);
  if (NULL != rh->dns_raw_packet)
    GNUNET_free (rh->dns_raw_packet);
  if (NULL != rh->namestore_task)
  {
    GNUNET_NAMESTORE_cancel (rh->namestore_task);
    rh->namestore_task = NULL;
  }
  if (GNUNET_SCHEDULER_NO_TASK != rh->dns_read_task)
    GNUNET_SCHEDULER_cancel (rh->dns_read_task);
  if (GNUNET_SCHEDULER_NO_TASK != rh->timeout_task)
    GNUNET_SCHEDULER_cancel (rh->timeout_task);
  if (NULL != rh->dns_sock)
    GNUNET_NETWORK_socket_close (rh->dns_sock);
  if (NULL != rh->dns_resolver_handle)
    GNUNET_RESOLVER_request_cancel (rh->dns_resolver_handle);
  if (NULL != rh->rd.data)
    GNUNET_free ((void*)(rh->rd.data));
  if (NULL != rh->dht_heap_node)
    GNUNET_CONTAINER_heap_remove_node (rh->dht_heap_node);
  GNUNET_free (rh);
}


/**
 * Callback when record data is put into namestore
 *
 * @param cls the closure
 * @param success GNUNET_OK on success
 * @param emsg the error message. NULL if SUCCESS==GNUNET_OK
 */
void
on_namestore_record_put_result (void *cls,
                                int32_t success,
                                const char *emsg)
{
  struct NamestoreBGTask *nbg = cls;

  GNUNET_CONTAINER_heap_remove_node (nbg->node);
  GNUNET_free (nbg);

  if (GNUNET_NO == success)
  {
    GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
               "GNS_NS: records already in namestore\n");
    return;
  }
  else if (GNUNET_YES == success)
  {
    GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
               "GNS_NS: records successfully put in namestore\n");
    return;
  }

  GNUNET_log(GNUNET_ERROR_TYPE_ERROR,
             "GNS_NS: Error putting records into namestore: %s\n", emsg);
}


/**
 * Function called when we get a result from the dht
 * for our record query
 *
 * @param cls the request handle
 * @param exp lifetime
 * @param key the key the record was stored under
 * @param get_path get path
 * @param get_path_length get path length
 * @param put_path put path
 * @param put_path_length put path length
 * @param type the block type
 * @param size the size of the record
 * @param data the record data
 */
static void
process_record_result_dht (void* cls,
                           struct GNUNET_TIME_Absolute exp,
                           const struct GNUNET_HashCode * key,
                           const struct GNUNET_PeerIdentity *get_path,
                           unsigned int get_path_length,
                           const struct GNUNET_PeerIdentity *put_path,
                           unsigned int put_path_length,
                           enum GNUNET_BLOCK_Type type,
                           size_t size, const void *data)
{
  struct ResolverHandle *rh = cls;
  struct RecordLookupHandle *rlh = rh->proc_cls;
  const struct GNSNameRecordBlock *nrb = data;
  uint32_t num_records;
  const char* name;
  const char* rd_data;
  uint32_t i;
  size_t rd_size;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "GNS_PHASE_REC-%llu: got dht result (size=%d)\n", rh->id, size);
  /* stop lookup and timeout task */
  GNUNET_DHT_get_stop (rh->get_handle);
  rh->get_handle = NULL;
  if (rh->dht_heap_node != NULL)
  {
    GNUNET_CONTAINER_heap_remove_node (rh->dht_heap_node);
    rh->dht_heap_node = NULL;
  }
  if (rh->timeout_task != GNUNET_SCHEDULER_NO_TASK)
  {
    GNUNET_SCHEDULER_cancel (rh->timeout_task);
    rh->timeout_task = GNUNET_SCHEDULER_NO_TASK;
  }
  rh->get_handle = NULL;
  name = (const char*) &nrb[1];
  num_records = ntohl (nrb->rd_count);
  {
    struct GNUNET_NAMESTORE_RecordData rd[num_records];
    struct NamestoreBGTask *ns_heap_root;
    struct NamestoreBGTask *namestore_bg_task;

    rd_data = &name[strlen (name) + 1];
    rd_size = size - strlen (name) - 1 - sizeof (struct GNSNameRecordBlock);
    if (GNUNET_SYSERR == 
	GNUNET_NAMESTORE_records_deserialize (rd_size,
					      rd_data,
					      num_records,
					      rd))
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                  "GNS_PHASE_REC-%llu: Error deserializing data!\n", rh->id);
      rh->proc (rh->proc_cls, rh, 0, NULL);
      return;
    }
    for (i = 0; i < num_records; i++)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "GNS_PHASE_REC-%llu: Got name: %s (wanted %s)\n",
                  rh->id, name, rh->name);
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "GNS_PHASE_REC-%llu: Got type: %d (wanted %d)\n",
                  rh->id, rd[i].record_type, rlh->record_type);
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "GNS_PHASE_REC-%llu: Got data length: %d\n",
                  rh->id, rd[i].data_size);
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "GNS_PHASE_REC-%llu: Got flag %d\n",
                  rh->id, rd[i].flags);

      if ((strcmp (name, rh->name) == 0) &&
          (rd[i].record_type == rlh->record_type))
        rh->answered++;

    }

    /**
     * FIXME check pubkey against existing key in namestore?
     * https://gnunet.org/bugs/view.php?id=2179
     */
    if (max_allowed_ns_tasks <=
        GNUNET_CONTAINER_heap_get_size (ns_task_heap))
    {
      ns_heap_root = GNUNET_CONTAINER_heap_remove_root (ns_task_heap);
      GNUNET_NAMESTORE_cancel (ns_heap_root->qe);

      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "GNS_PHASE_REC-%llu: Replacing oldest background ns task\n",
                  rh->id);
    }
    
    /* Save to namestore */
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "GNS_PHASE_REC-%llu: Caching record for %s\n",
                rh->id, name);
    namestore_bg_task = GNUNET_malloc (sizeof (struct NamestoreBGTask));
    namestore_bg_task->qe = GNUNET_NAMESTORE_record_put (namestore_handle,
                                 &nrb->public_key,
                                 name,
                                 exp,
                                 num_records,
                                 rd,
                                 &nrb->signature,
                                 &on_namestore_record_put_result, //cont
                                 namestore_bg_task);

    namestore_bg_task->node = GNUNET_CONTAINER_heap_insert (ns_task_heap,
                                  namestore_bg_task,
                                  GNUNET_TIME_absolute_get().abs_value_us);
    if (0 < rh->answered)
      rh->proc (rh->proc_cls, rh, num_records, rd);
    else
      rh->proc (rh->proc_cls, rh, 0, NULL);
  }
}


/**
 * Start DHT lookup for a (name -> query->record_type) record in
 * rh->authority's zone
 *
 * @param rh the pending gns query context
 */
static void
resolve_record_dht (struct ResolverHandle *rh)
{
  struct RecordLookupHandle *rlh = rh->proc_cls;
  uint32_t xquery;
  struct GNUNET_HashCode lookup_key;
  struct ResolverHandle *rh_heap_root;

  GNUNET_GNS_get_key_for_record (rh->name, &rh->authority, &lookup_key);
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "GNS_PHASE_REC-%llu: starting dht lookup for %s with key: %s\n",
              rh->id, rh->name, GNUNET_h2s (&lookup_key));

  rh->dht_heap_node = NULL;

  if (GNUNET_TIME_UNIT_FOREVER_REL.rel_value_us != rh->timeout.rel_value_us)
  {
    /**
     * Update timeout if necessary
     */
    if (GNUNET_SCHEDULER_NO_TASK == rh->timeout_task)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "GNS_PHASE_REC-%llu: Adjusting timeout to %s/2\n", 
		  rh->id,
		  GNUNET_STRINGS_relative_time_to_string (rh->timeout, GNUNET_YES));
      /*
       * Set timeout for authority lookup phase to 1/2
       */
      rh->timeout_task = GNUNET_SCHEDULER_add_delayed (
                                   GNUNET_TIME_relative_divide (rh->timeout, 2),
                                   &handle_lookup_timeout,
                                   rh);
    }
    rh->timeout_cont = &dht_lookup_timeout;
    rh->timeout_cont_cls = rh;
  }
  else 
  {
    if (max_allowed_background_queries <=
        GNUNET_CONTAINER_heap_get_size (dht_lookup_heap))
    {
      rh_heap_root = GNUNET_CONTAINER_heap_remove_root (dht_lookup_heap);
      GNUNET_DHT_get_stop (rh_heap_root->get_handle);
      rh_heap_root->get_handle = NULL;
      rh_heap_root->dht_heap_node = NULL;

      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                 "GNS_PHASE_REC-%llu: Replacing oldest background query for %s\n",
                 rh->id, rh_heap_root->name);
      rh_heap_root->proc (rh_heap_root->proc_cls,
                          rh_heap_root,
                          0,
                          NULL);
    }
    rh->dht_heap_node = GNUNET_CONTAINER_heap_insert (dht_lookup_heap,
                                         rh,
                                         GNUNET_TIME_absolute_get ().abs_value_us);
  }

  xquery = htonl (rlh->record_type);

  GNUNET_assert (rh->get_handle == NULL);
  rh->get_handle = GNUNET_DHT_get_start (dht_handle, 
                                         GNUNET_BLOCK_TYPE_GNS_NAMERECORD,
                                         &lookup_key,
                                         DHT_GNS_REPLICATION_LEVEL,
                                         GNUNET_DHT_RO_DEMULTIPLEX_EVERYWHERE,
                                         &xquery,
                                         sizeof (xquery),
                                         &process_record_result_dht,
                                         rh);

}


/**
 * Namestore calls this function if we have record for this name.
 * (or with rd_count=0 to indicate no matches)
 *
 * @param cls the pending query
 * @param key the key of the zone we did the lookup
 * @param expiration expiration date of the namestore entry
 * @param name the name for which we need an authority
 * @param rd_count the number of records with 'name'
 * @param rd the record data
 * @param signature the signature of the authority for the record data
 */
static void
process_record_result_ns (void* cls,
                          const struct GNUNET_CRYPTO_EccPublicKey *key,
                          struct GNUNET_TIME_Absolute expiration,
                          const char *name, unsigned int rd_count,
                          const struct GNUNET_NAMESTORE_RecordData *rd,
                          const struct GNUNET_CRYPTO_EccSignature *signature)
{
  struct ResolverHandle *rh = cls;
  struct RecordLookupHandle *rlh = rh->proc_cls;
  struct GNUNET_TIME_Relative remaining_time;
  struct GNUNET_CRYPTO_ShortHashCode zone;
  struct GNUNET_TIME_Absolute et;
  unsigned int i;

  rh->namestore_task = NULL;
  GNUNET_CRYPTO_short_hash (key,
			    sizeof (struct GNUNET_CRYPTO_EccPublicKey),
			    &zone);
  remaining_time = GNUNET_TIME_absolute_get_remaining (expiration);
  rh->status = 0;
  if (NULL != name)
  {
    rh->status |= RSL_RECORD_EXISTS;
    if (remaining_time.rel_value_us == 0)
      rh->status |= RSL_RECORD_EXPIRED;
  }
  if (0 == rd_count)
  {
    /**
     * Lookup terminated and no results
     */
    GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
               "GNS_PHASE_REC-%llu: Namestore lookup for %s terminated without results\n",
               rh->id, name);

    GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
               "GNS_PHASE_REC-%llu: Record %s unknown in namestore\n",
               rh->id, rh->name);
    /**
     * Our zone and no result? Cannot resolve TT
     */
    rh->proc(rh->proc_cls, rh, 0, NULL);
    return;

  }
  GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
             "GNS_PHASE_REC-%llu: Processing additional result %s from namestore\n",
             rh->id, name);
  for (i = 0; i < rd_count;i++)
  {
    if (rd[i].record_type != rlh->record_type)
      continue;

    if (ignore_pending_records &&
        (rd[i].flags & GNUNET_NAMESTORE_RF_PENDING))
    {
      GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
                 "GNS_PHASE_REC-%llu: Record %s is awaiting user confirmation. Skipping\n",
                 rh->id, name);
      continue;
    }
    
    //FIXME: eh? do I have to handle this here?
    GNUNET_break (0 == (rd[i].flags & GNUNET_NAMESTORE_RF_RELATIVE_EXPIRATION));
    et.abs_value_us = rd[i].expiration_time;
    if (0 == (GNUNET_TIME_absolute_get_remaining (et)).rel_value_us)
    {
      GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
                 "GNS_PHASE_REC-%llu: This record is expired. Skipping\n",
                 rh->id);
      continue;
    }
    rh->answered++;
  }

  /**
   * no answers found
   */
  if (0 == rh->answered)
  {
    GNUNET_log(GNUNET_ERROR_TYPE_DEBUG, 
               "GNS_PHASE_REC-%llu: No answers found. This is odd!\n", rh->id);
    rh->proc(rh->proc_cls, rh, 0, NULL);
    return;
  }

  GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
             "GNS_PHASE_REC-%llu: Found %d answer(s) to query in %d records!\n",
             rh->id, rh->answered, rd_count);
  rh->proc(rh->proc_cls, rh, rd_count, rd);
}


#ifndef WINDOWS
/**
 * VPN redirect result callback
 *
 * @param cls the resolver handle
 * @param af the requested address family
 * @param address in_addr(6) respectively
 */
static void
process_record_result_vpn (void* cls, int af, const void *address)
{
  struct ResolverHandle *rh = cls;
  struct RecordLookupHandle *rlh = rh->proc_cls;
  struct GNUNET_NAMESTORE_RecordData rd;

  GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
             "GNS_PHASE_REC_VPN-%llu: Got answer from VPN to query!\n",
             rh->id);
  if (AF_INET == af)
  {
    GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
               "GNS_PHASE_REC-%llu: Answer is IPv4!\n",
               rh->id);
    if (GNUNET_DNSPARSER_TYPE_A != rlh->record_type)
    {
      GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
                 "GNS_PHASE_REC-%llu: Requested record is not IPv4!\n",
                 rh->id);
      rh->proc (rh->proc_cls, rh, 0, NULL);
      return;
    }
    rd.record_type = GNUNET_DNSPARSER_TYPE_A;
    rd.expiration_time = UINT64_MAX; /* FIXME: should probably pick something shorter... */
    rd.data = address;
    rd.data_size = sizeof (struct in_addr);
    rd.flags = 0;
    rh->proc (rh->proc_cls, rh, 1, &rd);
    return;
  }
  else if (AF_INET6 == af)
  {
    GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
               "GNS_PHASE_REC-%llu: Answer is IPv6!\n",
               rh->id);
    if (GNUNET_DNSPARSER_TYPE_AAAA != rlh->record_type)
    {
      GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
                 "GNS_PHASE_REC-%llu: Requested record is not IPv6!\n",
                 rh->id);
      rh->proc (rh->proc_cls, rh, 0, NULL);
      return;
    }
    rd.record_type = GNUNET_DNSPARSER_TYPE_AAAA;
    rd.expiration_time = UINT64_MAX; /* FIXME: should probably pick something shorter... */
    rd.data = address;
    rd.data_size = sizeof (struct in6_addr);
    rd.flags = 0;
    rh->proc (rh->proc_cls, rh, 1, &rd);
    return;
  }

  GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
             "GNS_PHASE_REC-%llu: Got garbage from VPN!\n",
             rh->id);
  rh->proc (rh->proc_cls, rh, 0, NULL);
}
#endif


/**
 * Process VPN lookup result for record
 *
 * @param cls the record lookup handle
 * @param rh resolver handle
 * @param rd_count number of results (1)
 * @param rd record data containing the result
 */
static void
handle_record_vpn (void* cls, struct ResolverHandle *rh,
                   unsigned int rd_count,
                   const struct GNUNET_NAMESTORE_RecordData *rd)
{
  struct RecordLookupHandle* rlh = cls;
  
  if (0 == rd_count)
  {
    GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
               "GNS_PHASE_REC_VPN-%llu: VPN returned no records. (status: %d)!\n",
               rh->id,
               rh->status);
    /* give up, cannot resolve */
    finish_lookup(rh, rlh, 0, NULL);
    return;
  }

  /* results found yay */
  GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
             "GNS_PHASE_REC_VPN-%llu: Record resolved from VPN!\n",
	     rh->id);

  finish_lookup(rh, rlh, rd_count, rd);
}


/**
 * The final phase of resoution.
 * We found a NS RR and want to resolve via DNS
 *
 * @param rh the pending lookup handle
 * @param rd_count length of record data
 * @param rd record data containing VPN RR
 */
static void
resolve_record_dns (struct ResolverHandle *rh,
                    unsigned int rd_count,
                    const struct GNUNET_NAMESTORE_RecordData *rd)
{
  struct RecordLookupHandle *rlh = rh->proc_cls;
  struct GNUNET_DNSPARSER_Query query;
  struct GNUNET_DNSPARSER_Packet packet;
  struct GNUNET_DNSPARSER_Flags flags;
  struct in_addr dnsip;
  struct sockaddr_in addr;
  struct sockaddr *sa;
  unsigned int i;

  memset (&packet, 0, sizeof (struct GNUNET_DNSPARSER_Packet));
  memset (rh->dns_name, 0, sizeof (rh->dns_name));
  
  /* We cancel here as to not include the ns lookup in the timeout */
  if (GNUNET_SCHEDULER_NO_TASK != rh->timeout_task)
  {
    GNUNET_SCHEDULER_cancel(rh->timeout_task);
    rh->timeout_task = GNUNET_SCHEDULER_NO_TASK;
  }
  /* Start shortening */
  if ((NULL != rh->priv_key) &&
      (GNUNET_YES == is_canonical (rh->name)))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
             "GNS_PHASE_REC_DNS-%llu: Trying to shorten authority chain\n",
             rh->id);
    start_shorten (rh->authority_chain_head,
                   rh->priv_key);
  }

  for (i = 0; i < rd_count; i++)
  {
    /* Synthesize dns name */
    if (GNUNET_DNSPARSER_TYPE_NS == rd[i].record_type)
    {
      strcpy (rh->dns_zone, (char*)rd[i].data);
      if (0 == strcmp (rh->name, ""))
        strcpy (rh->dns_name, (char*)rd[i].data);
      else
        sprintf (rh->dns_name, "%s.%s", rh->name, (char*)rd[i].data);
    }
    /* The glue */
    if (GNUNET_DNSPARSER_TYPE_A == rd[i].record_type)
      /* need to use memcpy as .data may be unaligned */
      memcpy (&dnsip, rd[i].data, sizeof (dnsip));
  }
  
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "GNS_PHASE_REC_DNS-%llu: Looking up `%s' from `%s'\n",
              rh->id,
              rh->dns_name,
              inet_ntoa (dnsip));
  rh->dns_sock = GNUNET_NETWORK_socket_create (AF_INET, SOCK_DGRAM, 0);
  if (NULL == rh->dns_sock)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "GNS_PHASE_REC_DNS-%llu: Error creating udp socket for dns!\n",
                rh->id);
    finish_lookup (rh, rlh, 0, NULL);
    return;
  }

  memset (&addr, 0, sizeof (struct sockaddr_in));
  sa = (struct sockaddr *) &addr;
  sa->sa_family = AF_INET;
  if (GNUNET_OK != GNUNET_NETWORK_socket_bind (rh->dns_sock,
                                               sa,
                                               sizeof (struct sockaddr_in),
                                               0))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "GNS_PHASE_REC_DNS-%llu: Error binding UDP socket for DNS lookup!\n",
                rh->id);
    finish_lookup (rh, rlh, 0, NULL);
    return;
  }
  query.name = rh->dns_name;
  query.type = rlh->record_type;
  query.class = GNUNET_DNSPARSER_CLASS_INTERNET;
  memset (&flags, 0, sizeof (flags));
  flags.recursion_desired = 1;
  flags.checking_disabled = 1;
  packet.queries = &query;
  packet.answers = NULL;
  packet.authority_records = NULL;
  packet.num_queries = 1;
  packet.num_answers = 0;
  packet.num_authority_records = 0;
  packet.num_additional_records = 0;
  packet.flags = flags;
  packet.id = rh->id;
  if (GNUNET_OK != GNUNET_DNSPARSER_pack (&packet,
                                          UINT16_MAX,
                                          &rh->dns_raw_packet,
                                          &rh->dns_raw_packet_size))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "GNS_PHASE_REC_DNS-%llu: Creating raw dns packet!\n",
                rh->id);
    GNUNET_NETWORK_socket_close (rh->dns_sock);
    finish_lookup (rh, rlh, 0, NULL);
    return;
  }

  rh->dns_addr.sin_family = AF_INET;
  rh->dns_addr.sin_port = htons (53); //domain
  rh->dns_addr.sin_addr = dnsip;
#if HAVE_SOCKADDR_IN_SIN_LEN
  rh->dns_addr.sin_len = (u_char) sizeof (struct sockaddr_in);
#endif
  send_dns_packet (rh);
}


/**
 * The final phase of resoution.
 * We found a VPN RR and want to request an IPv4/6 address
 *
 * @param rh the pending lookup handle
 * @param rd_count length of record data
 * @param rd record data containing VPN RR
 */
static void
resolve_record_vpn (struct ResolverHandle *rh,
                    unsigned int rd_count,
                    const struct GNUNET_NAMESTORE_RecordData *rd)
{
  struct RecordLookupHandle *rlh = rh->proc_cls;
  struct GNUNET_HashCode serv_desc;
  struct vpn_data* vpn;
  int af;
  
  /* We cancel here as to not include the ns lookup in the timeout */
  if (GNUNET_SCHEDULER_NO_TASK != rh->timeout_task)
  {
    GNUNET_SCHEDULER_cancel(rh->timeout_task);
    rh->timeout_task = GNUNET_SCHEDULER_NO_TASK;
  }
  /* Start shortening */
  if ((NULL != rh->priv_key) &&
      (GNUNET_YES == is_canonical (rh->name)))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
             "GNS_PHASE_REC_VPN-%llu: Trying to shorten authority chain\n",
             rh->id);
    start_shorten (rh->authority_chain_head,
                   rh->priv_key);
  }

  vpn = (struct vpn_data*)rd->data;
  GNUNET_CRYPTO_hash ((char*)&vpn[1],
                      strlen ((char*)&vpn[1]) + 1,
                      &serv_desc);
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "GNS_PHASE_REC_VPN-%llu: proto %hu peer %s!\n",
              rh->id,
              ntohs (vpn->proto),
              GNUNET_h2s (&vpn->peer));

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "GNS_PHASE_REC_VPN-%llu: service %s -> %s!\n",
              rh->id,
              (char*)&vpn[1],
              GNUNET_h2s (&serv_desc));
  rh->proc = &handle_record_vpn;
  if (GNUNET_DNSPARSER_TYPE_A == rlh->record_type)
    af = AF_INET;
  else
    af = AF_INET6;
#ifndef WINDOWS
  if (NULL == vpn_handle)
  {
    vpn_handle = GNUNET_VPN_connect (cfg);
    if (NULL == vpn_handle)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "GNS_PHASE_INIT: Error connecting to VPN!\n");
      finish_lookup (rh, rh->proc_cls, 0, NULL);
      return;
    }
  }

  rh->vpn_handle = GNUNET_VPN_redirect_to_peer (vpn_handle,
						af, ntohs (vpn->proto),
						(struct GNUNET_PeerIdentity *)&vpn->peer,
						&serv_desc,
						GNUNET_NO, //nac
						GNUNET_TIME_UNIT_FOREVER_ABS, //FIXME
						&process_record_result_vpn,
						rh);
#else
  GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
	      "Error connecting to VPN (not available on W32 yet)\n");
  finish_lookup (rh, rh->proc_cls, 0, NULL);  
#endif
}


/**
 * The final phase of resolution.
 * rh->name is a name that is canonical and we do not have a delegation.
 * Query namestore for this record
 *
 * @param rh the pending lookup handle
 */
static void
resolve_record_ns(struct ResolverHandle *rh)
{
  struct RecordLookupHandle *rlh = rh->proc_cls;
  
  /* We cancel here as to not include the ns lookup in the timeout */
  if (GNUNET_SCHEDULER_NO_TASK != rh->timeout_task)
  {
    GNUNET_SCHEDULER_cancel(rh->timeout_task);
    rh->timeout_task = GNUNET_SCHEDULER_NO_TASK;
  }
  /* Start shortening */
  if ((NULL != rh->priv_key) &&
     (GNUNET_YES == is_canonical (rh->name)))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
             "GNS_PHASE_REC-%llu: Trying to shorten authority chain\n",
             rh->id);
    start_shorten (rh->authority_chain_head,
                   rh->priv_key);
  }
  
  /**
   * Try to resolve this record in our namestore.
   * The name to resolve is now in rh->authority_name
   * since we tried to resolve it to an authority
   * and failed.
   **/
  rh->namestore_task = GNUNET_NAMESTORE_lookup_record(namestore_handle,
                                 &rh->authority,
                                 rh->name,
                                 rlh->record_type,
                                 &process_record_result_ns,
                                 rh);
}


/**
 * Handle timeout for DHT requests
 *
 * @param cls the request handle as closure
 * @param tc the task context
 */
static void
dht_authority_lookup_timeout (void *cls,
                              const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct ResolverHandle *rh = cls;
  struct RecordLookupHandle *rlh = rh->proc_cls;
  char new_name[GNUNET_DNSPARSER_MAX_NAME_LENGTH];

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
	      "GNS_PHASE_DELEGATE_DHT-%llu: dht lookup for query %s (%s) timed out.\n",
	      rh->id, rh->authority_name, 
	      GNUNET_STRINGS_relative_time_to_string (rh->timeout,
						      GNUNET_YES));

  rh->status |= RSL_TIMED_OUT;
  rh->timeout_task = GNUNET_SCHEDULER_NO_TASK;
  if (NULL != rh->get_handle)
  {
    GNUNET_DHT_get_stop (rh->get_handle);
    rh->get_handle = NULL;
  }
  if (0 != (tc->reason & GNUNET_SCHEDULER_REASON_SHUTDOWN))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		"GNS_PHASE_DELEGATE_DHT-%llu: Got shutdown\n",
		rh->id);
    rh->proc (rh->proc_cls, rh, 0, NULL);
    return;
  }
  if (0 == strcmp (rh->name, ""))
  {
    /*
     * promote authority back to name and try to resolve record
     */
    strcpy (rh->name, rh->authority_name);
    rh->proc (rh->proc_cls, rh, 0, NULL);
    return;
  }
  
  /**
   * Start resolution in bg
   */
  GNUNET_snprintf (new_name, GNUNET_DNSPARSER_MAX_NAME_LENGTH,
		   "%s.%s.%s", 
		   rh->name, rh->authority_name, GNUNET_GNS_TLD);
  strcpy (rh->name, new_name);
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
	      "GNS_PHASE_DELEGATE_DHT-%llu: Starting background query for %s type %d\n",
	      rh->id, rh->name,
	      rlh->record_type);
  gns_resolver_lookup_record (rh->authority,
                              rh->private_local_zone,
                              rlh->record_type,
                              new_name,
                              NULL,
                              GNUNET_TIME_UNIT_FOREVER_REL,
                              GNUNET_NO,
                              &background_lookup_result_processor,
                              NULL);
  rh->proc (rh->proc_cls, rh, 0, NULL);
}


/**
 * This is a callback function that checks for key revocation
 *
 * @param cls the pending query
 * @param key the key of the zone we did the lookup
 * @param expiration expiration date of the record data set in the namestore
 * @param name the name for which we need an authority
 * @param rd_count the number of records with 'name'
 * @param rd the record data
 * @param signature the signature of the authority for the record data
 */
static void
process_pkey_revocation_result_ns (void *cls,
				   const struct GNUNET_CRYPTO_EccPublicKey *key,
				   struct GNUNET_TIME_Absolute expiration,
				   const char *name,
				   unsigned int rd_count,
				   const struct GNUNET_NAMESTORE_RecordData *rd,
				   const struct GNUNET_CRYPTO_EccSignature *signature)
{
  struct ResolverHandle *rh = cls;
  struct GNUNET_TIME_Relative remaining_time;
  int i;
  
  rh->namestore_task = NULL;
  remaining_time = GNUNET_TIME_absolute_get_remaining (expiration);
  
  for (i = 0; i < rd_count; i++)
  {
    if (GNUNET_NAMESTORE_TYPE_REV == rd[i].record_type)
    {
      GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
                 "GNS_PHASE_DELEGATE_REV-%llu: Zone has been revoked.\n",
                 rh->id);
      rh->status |= RSL_PKEY_REVOKED;
      rh->proc (rh->proc_cls, rh, 0, NULL);
      return;
    }
  }
  
  if ((NULL == name) ||
      (0 == remaining_time.rel_value_us))
  {
    GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
          "GNS_PHASE_DELEGATE_REV-%llu: + Records don't exist or are expired.\n",
          rh->id, name);

    if (GNUNET_TIME_UNIT_FOREVER_REL.rel_value_us != rh->timeout.rel_value_us)
    {
      GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
        "GNS_PHASE_DELEGATE_REV-%llu: Starting background lookup for %s type %d\n",
        rh->id, "+.gads", GNUNET_NAMESTORE_TYPE_REV);

      gns_resolver_lookup_record(rh->authority,
                                 rh->private_local_zone,
                                 GNUNET_NAMESTORE_TYPE_REV,
                                 GNUNET_GNS_TLD,
                                 NULL,
                                 GNUNET_TIME_UNIT_FOREVER_REL,
                                 GNUNET_NO,
                                 &background_lookup_result_processor,
                                 NULL);
    }
  }
 GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
             "GNS_PHASE_DELEGATE_REV-%llu: Revocation check passed\n",
             rh->id);
  /**
   * We are done with PKEY resolution if name is empty
   * else resolve again with new authority
   */
  if (strcmp (rh->name, "") == 0)
    rh->proc (rh->proc_cls, rh, rh->rd_count, &rh->rd);
  else
    resolve_delegation_ns (rh);
}


/**
 * Callback when record data is put into namestore
 *
 * @param cls the closure
 * @param success GNUNET_OK on success
 * @param emsg the error message. NULL if SUCCESS==GNUNET_OK
 */
static void
on_namestore_delegation_put_result(void *cls,
                                   int32_t success,
                                   const char *emsg)
{
  struct NamestoreBGTask *nbg = cls;

  GNUNET_CONTAINER_heap_remove_node (nbg->node);
  GNUNET_free (nbg);

  if (GNUNET_NO == success)
  {
    GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
               "GNS_NS: records already in namestore\n");
    return;
  }
  else if (GNUNET_YES == success)
  {
    GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
               "GNS_NS: records successfully put in namestore\n");
    return;
  }

  GNUNET_log(GNUNET_ERROR_TYPE_ERROR,
             "GNS_NS: Error putting records into namestore: %s\n", emsg);
}


/**
 * Function called when we get a result from the dht
 * for our query. Recursively tries to resolve authorities
 * for name in DHT.
 *
 * @param cls the request handle
 * @param exp lifetime
 * @param key the key the record was stored under
 * @param get_path get path
 * @param get_path_length get path length
 * @param put_path put path
 * @param put_path_length put path length
 * @param type the block type
 * @param size the size of the record
 * @param data the record data
 */
static void
process_delegation_result_dht (void* cls,
			       struct GNUNET_TIME_Absolute exp,
			       const struct GNUNET_HashCode * key,
			       const struct GNUNET_PeerIdentity *get_path,
			       unsigned int get_path_length,
			       const struct GNUNET_PeerIdentity *put_path,
			       unsigned int put_path_length,
			       enum GNUNET_BLOCK_Type type,
			       size_t size, const void *data)
{
  struct ResolverHandle *rh = cls;
  const struct GNSNameRecordBlock *nrb = data;
  const char* rd_data;
  uint32_t num_records;
  const char* name;
  uint32_t i;
  int rd_size;
  struct GNUNET_CRYPTO_ShortHashCode zone;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
	      "GNS_PHASE_DELEGATE_DHT-%llu: Got DHT result\n",
	      rh->id);
  if (data == NULL)
    return;
   /* stop dht lookup and timeout task */
  GNUNET_DHT_get_stop (rh->get_handle);
  rh->get_handle = NULL;
  if (rh->dht_heap_node != NULL)
  {
    GNUNET_CONTAINER_heap_remove_node(rh->dht_heap_node);
    rh->dht_heap_node = NULL;
  }

  num_records = ntohl(nrb->rd_count);
  name = (const char*) &nrb[1];
  {
    struct GNUNET_NAMESTORE_RecordData rd[num_records];
    struct NamestoreBGTask *ns_heap_root;
    struct NamestoreBGTask *namestore_bg_task;
    
    rd_data = name + strlen(name) + 1;
    rd_size = size - strlen(name) - 1 - sizeof (struct GNSNameRecordBlock);
    if (GNUNET_SYSERR == GNUNET_NAMESTORE_records_deserialize (rd_size,
                                                               rd_data,
                                                               num_records,
                                                               rd))
    {
      GNUNET_log(GNUNET_ERROR_TYPE_ERROR,
                 "GNS_PHASE_DELEGATE_DHT-%llu: Error deserializing data!\n",
                 rh->id);
      return;
    }

    GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
               "GNS_PHASE_DELEGATE_DHT-%llu: Got name: %s (wanted %s)\n",
               rh->id, name, rh->authority_name);
    for (i=0; i<num_records; i++)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		  "GNS_PHASE_DELEGATE_DHT-%llu: Got name: %s (wanted %s)\n",
		  rh->id, name, rh->authority_name);
      GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
                 "GNS_PHASE_DELEGATE_DHT-%llu: Got type: %d (wanted %d)\n",
                 rh->id, rd[i].record_type, GNUNET_NAMESTORE_TYPE_PKEY);
      GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
                 "GNS_PHASE_DELEGATE_DHT-%llu: Got data length: %d\n",
                 rh->id, rd[i].data_size);
      GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
                 "GNS_PHASE_DELEGATE_DHT-%llu: Got flag %d\n",
                 rh->id, rd[i].flags);
      
      if ((GNUNET_NAMESTORE_TYPE_VPN == rd[i].record_type) ||
          (GNUNET_DNSPARSER_TYPE_NS == rd[i].record_type) ||
          (GNUNET_DNSPARSER_TYPE_CNAME == rd[i].record_type))
      {
        /**
         * This is a VPN,NS,CNAME entry. Let namestore handle this after caching
         */
        if (0 == strcmp(rh->name, ""))
          strcpy(rh->name, rh->authority_name);
        else
          GNUNET_snprintf(rh->name, GNUNET_DNSPARSER_MAX_NAME_LENGTH, "%s.%s",
                 rh->name, rh->authority_name); //FIXME ret
        rh->answered = 1;
        break;
      }

      if ((0 == strcmp(name, rh->authority_name)) &&
          (GNUNET_NAMESTORE_TYPE_PKEY == rd[i].record_type))
      {
        GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
                   "GNS_PHASE_DELEGATE_DHT-%llu: Authority found in DHT\n",
                   rh->id);
        rh->answered = 1;
        memcpy(&rh->authority, rd[i].data, sizeof(struct GNUNET_CRYPTO_ShortHashCode));
        struct AuthorityChain *auth =
          GNUNET_malloc(sizeof(struct AuthorityChain));
        auth->zone = rh->authority;
        memset(auth->name, 0, strlen(rh->authority_name)+1);
        strcpy(auth->name, rh->authority_name);
        GNUNET_CONTAINER_DLL_insert (rh->authority_chain_head,
                                     rh->authority_chain_tail,
                                     auth);

        if (NULL != rh->rd.data)
          GNUNET_free ((void*)rh->rd.data);
        
        memcpy (&rh->rd, &rd[i], sizeof (struct GNUNET_NAMESTORE_RecordData));
        rh->rd.data = GNUNET_malloc (rd[i].data_size);
        memcpy ((void*)(rh->rd.data), rd[i].data, rd[i].data_size);
        rh->rd_count = 1;

        /** try to import pkey if private key available */
        //if (rh->priv_key && is_canonical (rh->name))
        //  process_discovered_authority(name, auth->zone,
        //                               rh->authority_chain_tail->zone,
        //                               rh->priv_key);
      }

    }
    GNUNET_GNS_get_zone_from_key (name, key, &zone);


    /* Save to namestore
    if (0 != GNUNET_CRYPTO_short_hash_cmp(&rh->authority_chain_head->zone,
                                          &zone))
    {*/
      if (max_allowed_ns_tasks <=
          GNUNET_CONTAINER_heap_get_size (ns_task_heap))
      {
        ns_heap_root = GNUNET_CONTAINER_heap_remove_root (ns_task_heap);
        GNUNET_NAMESTORE_cancel (ns_heap_root->qe);

        GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
                   "GNS_PHASE_DELEGATE_DHT-%llu: Replacing oldest background ns task\n",
                   rh->id);
      }
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "GNS_PHASE_DELEGATE_DHT-%llu: Caching record for %s\n",
                  rh->id, name);
      namestore_bg_task = GNUNET_malloc (sizeof (struct NamestoreBGTask));

      namestore_bg_task->node = GNUNET_CONTAINER_heap_insert (ns_task_heap,
                                    namestore_bg_task,
                                    GNUNET_TIME_absolute_get().abs_value_us);
      namestore_bg_task->qe = GNUNET_NAMESTORE_record_put (namestore_handle,
                                 &nrb->public_key,
                                 name,
                                 exp,
                                 num_records,
                                 rd,
                                 &nrb->signature,
                                 &on_namestore_delegation_put_result, //cont
                                 namestore_bg_task); //cls
    }
  //}

  if (0 != rh->answered)
  {
    rh->answered = 0;
    /**
     * delegate
     * FIXME in this case. should we ask namestore again?
     */
    GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
    "GNS_PHASE_DELEGATE_DHT-%llu: Answer from DHT for %s. Yet to resolve: %s\n",
    rh->id, rh->authority_name, rh->name);

    if (0 == strcmp(rh->name, ""))
    {
      /* Start shortening */
      if ((NULL != rh->priv_key) &&
          (GNUNET_YES == is_canonical (rh->name)))
      {
        GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
             "GNS_PHASE_DELEGATE_DHT-%llu: Trying to shorten authority chain\n",
             rh->id);
        start_shorten (rh->authority_chain_head,
                       rh->priv_key);
      }
    }
    else
      rh->proc = &handle_delegation_ns;


    /* Check for key revocation and delegate */
    rh->namestore_task = GNUNET_NAMESTORE_lookup_record (namestore_handle,
                                    &rh->authority,
                                    GNUNET_GNS_MASTERZONE_STR,
                                    GNUNET_NAMESTORE_TYPE_REV,
                                    &process_pkey_revocation_result_ns,
                                    rh);

    return;
  }
  
  /**
   * No pkey but name exists
   * promote back
   */
  GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
             "GNS_PHASE_DELEGATE_DHT-%llu: Adding %s back to %s\n",
             rh->id, rh->authority_name, rh->name);
  if (0 == strcmp(rh->name, ""))
    strcpy(rh->name, rh->authority_name);
  else
    GNUNET_snprintf(rh->name, GNUNET_DNSPARSER_MAX_NAME_LENGTH, "%s.%s",
                  rh->name, rh->authority_name); //FIXME ret
  
  GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
             "GNS_PHASE_DELEGATE_DHT-%llu: %s restored\n", rh->id, rh->name);
  GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
          "GNS_PHASE_DELEGATE_DHT-%llu: DHT authority lookup found no match!\n",
           rh->id);
  rh->proc(rh->proc_cls, rh, 0, NULL);
}

//FIXME maybe define somewhere else?
#define MAX_SOA_LENGTH sizeof(uint32_t)+sizeof(uint32_t)+sizeof(uint32_t)+sizeof(uint32_t)\
                        +(GNUNET_DNSPARSER_MAX_NAME_LENGTH*2)
#define MAX_MX_LENGTH sizeof(uint16_t)+GNUNET_DNSPARSER_MAX_NAME_LENGTH
#define MAX_SRV_LENGTH (sizeof(uint16_t)*3)+GNUNET_DNSPARSER_MAX_NAME_LENGTH


/**
 * Exands a name ending in .+ with the zone of origin.
 * FIXME: funky api: 'dest' must be large enough to hold
 * the result; this is a bit yucky...
 *
 * @param dest destination buffer
 * @param src the .+ name
 * @param repl the string to replace the + with
 */
static void
expand_plus (char* dest, 
	     const char* src, 
	     const char* repl)
{
  char* pos;
  size_t s_len = strlen (src) + 1;

  //Eh? I guess this is at least strlen ('x.+') == 3 FIXME
  if (3 > s_len)
  {
    /* no postprocessing */
    GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
               "GNS_POSTPROCESS: %s too short\n", src);
    memcpy (dest, src, s_len);
    return;
  }
  if (0 == strcmp (src + s_len - 3, ".+"))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		"GNS_POSTPROCESS: Expanding .+ in %s\n", 
		src);
    memset (dest, 0, s_len + strlen (repl) + strlen(GNUNET_GNS_TLD));
    strcpy (dest, src);
    pos = dest + s_len - 2;
    strcpy (pos, repl);
    pos += strlen (repl);
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		"GNS_POSTPROCESS: Expanded to %s\n", 
		dest);
  }
  else
  {
    GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
               "GNS_POSTPROCESS: No postprocessing for %s\n", src);
    memcpy (dest, src, s_len);
  }
}


/**
 * finish lookup
 */
static void
finish_lookup (struct ResolverHandle *rh,
               struct RecordLookupHandle* rlh,
               unsigned int rd_count,
               const struct GNUNET_NAMESTORE_RecordData *rd)
{
  unsigned int i;
  char new_rr_data[GNUNET_DNSPARSER_MAX_NAME_LENGTH];
  char new_mx_data[MAX_MX_LENGTH];
  char new_soa_data[MAX_SOA_LENGTH];
  char new_srv_data[MAX_SRV_LENGTH];
  struct srv_data *old_srv;
  struct srv_data *new_srv;
  struct soa_data *old_soa;
  struct soa_data *new_soa;
  struct GNUNET_NAMESTORE_RecordData p_rd[rd_count];
  char* repl_string;
  char* pos;
  unsigned int offset;

  if (GNUNET_SCHEDULER_NO_TASK != rh->timeout_task)
  {
    GNUNET_SCHEDULER_cancel(rh->timeout_task);
    rh->timeout_task = GNUNET_SCHEDULER_NO_TASK;
  }

  GNUNET_CONTAINER_DLL_remove (rlh_head, rlh_tail, rh);

  if (0 < rd_count)
    memcpy(p_rd, rd, rd_count*sizeof(struct GNUNET_NAMESTORE_RecordData));

  for (i = 0; i < rd_count; i++)
  {
    
    if ((GNUNET_DNSPARSER_TYPE_NS != rd[i].record_type) &&
        (GNUNET_DNSPARSER_TYPE_PTR != rd[i].record_type) &&
        (GNUNET_DNSPARSER_TYPE_CNAME != rd[i].record_type) &&
        (GNUNET_DNSPARSER_TYPE_MX != rd[i].record_type) &&
        (GNUNET_DNSPARSER_TYPE_SOA != rd[i].record_type) &&
        (GNUNET_DNSPARSER_TYPE_SRV != rd[i].record_type))
    {
      p_rd[i].data = rd[i].data;
      continue;
    }

    /**
     * for all those records we 'should'
     * also try to resolve the A/AAAA records (RFC1035)
     * This is a feature and not important
     */
    
    GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
               "GNS_POSTPROCESS: Postprocessing\n");
    if (0 == strcmp(rh->name, GNUNET_GNS_MASTERZONE_STR))
      repl_string = rlh->name;
    else
      repl_string = rlh->name+strlen(rh->name)+1;

    offset = 0;
    if (GNUNET_DNSPARSER_TYPE_MX == rd[i].record_type)
    {
      memcpy (new_mx_data, (char*)rd[i].data, sizeof(uint16_t));
      offset = sizeof (uint16_t);
      pos = new_mx_data + offset;
      // FIXME: how do we know that 'pos' has enough space for the new name?
      expand_plus (pos, (char*)rd[i].data+sizeof(uint16_t),
		   repl_string);
      offset += strlen(new_mx_data+sizeof(uint16_t)) + 1;
      p_rd[i].data = new_mx_data;
      p_rd[i].data_size = offset;
    }
    else if (GNUNET_DNSPARSER_TYPE_SRV == rd[i].record_type)
    {
      /*
       * Prio, weight and port
       */
      new_srv = (struct srv_data*)new_srv_data;
      old_srv = (struct srv_data*)rd[i].data;
      new_srv->prio = old_srv->prio;
      new_srv->weight = old_srv->weight;
      new_srv->port = old_srv->port;
      // FIXME: how do we know that '&new_srv[1]' has enough space for the new name?
      expand_plus((char*)&new_srv[1], (char*)&old_srv[1],
                  repl_string);
      p_rd[i].data = new_srv_data;
      p_rd[i].data_size = sizeof (struct srv_data) + strlen ((char*)&new_srv[1]) + 1;
    }
    else if (GNUNET_DNSPARSER_TYPE_SOA == rd[i].record_type)
    {
      /* expand mname and rname */
      old_soa = (struct soa_data*)rd[i].data;
      new_soa = (struct soa_data*)new_soa_data;
      memcpy (new_soa, old_soa, sizeof (struct soa_data));
      // FIXME: how do we know that 'new_soa[1]' has enough space for the new name?
      expand_plus((char*)&new_soa[1], (char*)&old_soa[1], repl_string);
      offset = strlen ((char*)&new_soa[1]) + 1;
      // FIXME: how do we know that 'new_soa[1]' has enough space for the new name?
      expand_plus((char*)&new_soa[1] + offset,
                  (char*)&old_soa[1] + strlen ((char*)&old_soa[1]) + 1,
                  repl_string);
      p_rd[i].data_size = sizeof (struct soa_data)
                          + offset
                          + strlen ((char*)&new_soa[1] + offset);
      p_rd[i].data = new_soa_data;
    }
    else
    {
      pos = new_rr_data;
      // FIXME: how do we know that 'rd[i].data' has enough space for the new name?
      expand_plus(pos, (char*)rd[i].data, repl_string);
      p_rd[i].data_size = strlen(new_rr_data)+1;
      p_rd[i].data = new_rr_data;
    }
    
  }

  rlh->proc(rlh->proc_cls, rd_count, p_rd);
  GNUNET_free(rlh);
  free_resolver_handle (rh);
}


/**
 * Process DHT lookup result for record.
 *
 * @param cls the closure
 * @param rh resolver handle
 * @param rd_count number of results
 * @param rd record data
 */
static void
handle_record_dht (void* cls, struct ResolverHandle *rh,
		   unsigned int rd_count,
		   const struct GNUNET_NAMESTORE_RecordData *rd)
{
  struct RecordLookupHandle* rlh = cls;

  if (0 == rd_count)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
               "GNS_PHASE_REC-%llu: No records for %s found in DHT. Aborting\n",
               rh->id, rh->name);
    /* give up, cannot resolve */
    finish_lookup (rh, rlh, 0, NULL);
    return;
  }
  /* results found yay */
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
             "GNS_PHASE_REC-%llu: Record resolved from DHT!", rh->id);
  finish_lookup (rh, rlh, rd_count, rd);
}


/**
 * Process namestore lookup result for record.
 *
 * @param cls the closure
 * @param rh resolver handle
 * @param rd_count number of results
 * @param rd record data
 */
static void
handle_record_ns (void* cls, struct ResolverHandle *rh,
                  unsigned int rd_count,
                  const struct GNUNET_NAMESTORE_RecordData *rd)
{
  struct RecordLookupHandle* rlh = cls;
  int check_dht = GNUNET_YES;
  
  if (0 != rd_count)
  {
    /* results found yay */
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
               "GNS_PHASE_REC-%llu: Record resolved from namestore!\n", rh->id);
    finish_lookup (rh, rlh, rd_count, rd);
    return;
  }
  
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "GNS_PHASE_REC-%llu: NS returned no records. (status: %d)!\n",
              rh->id,
              rh->status);
  /**
   * There are 5 conditions that have to met for us to consult the DHT:
   * 1. The entry in the DHT is RSL_RECORD_EXPIRED OR
   * 2. No entry in the NS existed AND
   * 3. The zone queried is not the local resolver's zone AND
   * 4. The name that was looked up is '+'
   *    because if it was any other canonical name we either already queried
   *    the DHT for the authority in the authority lookup phase (and thus
   *    would already have an entry in the NS for the record)
   * 5. We are not in cache only mode
   */
  if ((0 != (rh->status & RSL_RECORD_EXPIRED)) &&
      (0 == (rh->status & RSL_RECORD_EXISTS)) )
  {
    
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "GNS_PHASE_REC-%llu: Not expired and exists!\n",
              rh->id);
    check_dht = GNUNET_NO;
  }
  
  if (0 == GNUNET_CRYPTO_short_hash_cmp(&rh->authority_chain_head->zone,
                                        &rh->private_local_zone))
  {

    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "GNS_PHASE_REC-%llu: Our zone!\n",
              rh->id);
    check_dht = GNUNET_NO;
  }
  
  if ((0 != strcmp (rh->name, GNUNET_GNS_MASTERZONE_STR)) && (GNUNET_YES == is_srv (rh->name)))
      check_dht = GNUNET_NO;

  if (GNUNET_YES == rh->only_cached)
    check_dht = GNUNET_NO;
  
  if (GNUNET_YES == check_dht)
  {
    rh->proc = &handle_record_dht;
    resolve_record_dht(rh);
    return;
  }
  /* give up, cannot resolve */
  finish_lookup (rh, rlh, 0, NULL);
}


/**
 * Move one level up in the domain hierarchy and return the
 * passed top level domain.
 *
 * FIXME: funky API: not only 'dest' is updated, so is 'name'!
 *
 * @param name the domain
 * @param dest the destination where the tld will be put
 */
static void
pop_tld (char* name, char* dest)
{
  uint32_t len;

  if (GNUNET_YES == is_canonical (name))
  {
    strcpy (dest, name);
    strcpy (name, "");
    return;
  }

  for (len = strlen(name); 0 < len; len--)
  {
    if (*(name+len) == '.')
      break;
  }
  
  //Was canonical?
  if (0 == len)
    return;
  name[len] = '\0';
  strcpy (dest, (name+len+1));
}


/**
 * DHT resolution for delegation finished. Processing result.
 *
 * @param cls the closure
 * @param rh resolver handle
 * @param rd_count number of results (always 0)
 * @param rd record data (always NULL)
 */
static void
handle_delegation_dht(void* cls, struct ResolverHandle *rh,
                          unsigned int rd_count,
                          const struct GNUNET_NAMESTORE_RecordData *rd)
{
  struct RecordLookupHandle* rlh = cls;
  
  if (0 == strcmp(rh->name, ""))
  {
    if (GNUNET_NAMESTORE_TYPE_PKEY == rlh->record_type)
    {
      GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
                 "GNS_PHASE_DELEGATE_DHT-%llu: Resolved queried PKEY via DHT.\n",
                 rh->id);
      finish_lookup(rh, rlh, rd_count, rd);
      return;
    }
    /* We resolved full name for delegation. resolving record */
    GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
     "GNS_PHASE_DELEGATE_DHT-%llu: Resolved full name for delegation via DHT.\n",
     rh->id);
    strcpy(rh->name, "+\0");
    rh->proc = &handle_record_ns;
    resolve_record_ns(rh);
    return;
  }

  /**
   * we still have some left
   **/
  if (GNUNET_YES == is_canonical (rh->name))
  {
    GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
             "GNS_PHASE_DELEGATE_DHT-%llu: Resolving canonical record %s in ns\n",
             rh->id,
             rh->name);
    rh->proc = &handle_record_ns;
    resolve_record_ns(rh);
    return;
  }
  /* give up, cannot resolve */
  GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
 "GNS_PHASE_DELEGATE_DHT-%llu: Cannot fully resolve delegation for %s via DHT!\n",
 rh->id, rh->name);
  finish_lookup(rh, rlh, 0, NULL);
}


/**
 * Start DHT lookup for a name -> PKEY (compare NS) record in
 * rh->authority's zone
 *
 * @param rh the pending gns query
 */
static void
resolve_delegation_dht (struct ResolverHandle *rh)
{
  uint32_t xquery;
  struct GNUNET_HashCode lookup_key;
  struct ResolverHandle *rh_heap_root;
  
  pop_tld (rh->name, rh->authority_name);
  GNUNET_GNS_get_key_for_record (rh->authority_name,
				 &rh->authority, 
				 &lookup_key);
  rh->dht_heap_node = NULL;
  if (rh->timeout.rel_value_us != GNUNET_TIME_UNIT_FOREVER_REL.rel_value_us)
  {
    rh->timeout_cont = &dht_authority_lookup_timeout;
    rh->timeout_cont_cls = rh;
  }
  else 
  {
    if (max_allowed_background_queries <=
        GNUNET_CONTAINER_heap_get_size (dht_lookup_heap))
    {
      /* terminate oldest lookup */
      rh_heap_root = GNUNET_CONTAINER_heap_remove_root (dht_lookup_heap);
      GNUNET_DHT_get_stop (rh_heap_root->get_handle);
      rh_heap_root->get_handle = NULL;
      rh_heap_root->dht_heap_node = NULL;
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		  "GNS_PHASE_DELEGATE_DHT-%llu: Replacing oldest background query for %s\n",
		  rh->id, 
		  rh_heap_root->authority_name);
      rh_heap_root->proc (rh_heap_root->proc_cls,
			  rh_heap_root,
			  0,
			  NULL);
    }
    rh->dht_heap_node = GNUNET_CONTAINER_heap_insert (dht_lookup_heap,
						      rh,
						      GNUNET_TIME_absolute_get().abs_value_us);
  }
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
	      "Beginning DHT lookup for %s in zone %s for request %llu\n",
	      rh->authority_name,
	      GNUNET_short_h2s (&rh->authority),
	      rh->id);
  xquery = htonl (GNUNET_NAMESTORE_TYPE_PKEY);
  GNUNET_assert (rh->get_handle == NULL);
  rh->get_handle = GNUNET_DHT_get_start (dht_handle,
					 GNUNET_BLOCK_TYPE_GNS_NAMERECORD,
					 &lookup_key,
					 DHT_GNS_REPLICATION_LEVEL,
					 GNUNET_DHT_RO_DEMULTIPLEX_EVERYWHERE,
					 &xquery,
					 sizeof(xquery),
					 &process_delegation_result_dht,
					 rh);
}


/**
 * Namestore resolution for delegation finished. Processing result.
 *
 * @param cls the closure
 * @param rh resolver handle
 * @param rd_count number of results (always 0)
 * @param rd record data (always NULL)
 */
static void
handle_delegation_ns (void* cls, struct ResolverHandle *rh,
                      unsigned int rd_count,
                      const struct GNUNET_NAMESTORE_RecordData *rd)
{
  struct RecordLookupHandle* rlh = cls;
  int check_dht;
  size_t s_len;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
	      "GNS_PHASE_DELEGATE_NS-%llu: Resolution status: %d.\n",
	      rh->id, rh->status);

  if (rh->status & RSL_PKEY_REVOKED)
  {
    finish_lookup (rh, rlh, 0, NULL);
    return;
  }
  
  if (0 == strcmp(rh->name, ""))
  {
    
    /* We resolved full name for delegation. resolving record */
    GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
              "GNS_PHASE_DELEGATE_NS-%llu: Resolved full name for delegation.\n",
              rh->id);
    if (rh->status & RSL_CNAME_FOUND)
    {
      if (GNUNET_DNSPARSER_TYPE_CNAME == rlh->record_type)
      {
        GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
                  "GNS_PHASE_DELEGATE_NS-%llu: Resolved queried CNAME in NS.\n",
                  rh->id);
        strcpy (rh->name, rh->authority_name);
        finish_lookup (rh, rlh, rd_count, rd);
        return;
      }
      
      /* A .+ CNAME  */
      if (GNUNET_YES == is_tld ((char*)rd->data, GNUNET_GNS_TLD_PLUS))
      {
        s_len = strlen (rd->data) - 2;
        memcpy (rh->name, rd->data, s_len);
        rh->name[s_len] = '\0';
        resolve_delegation_ns (rh);
        return;
      }
      else if (GNUNET_YES == is_tld ((char*)rd->data, GNUNET_GNS_TLD_ZKEY))
      {
        gns_resolver_lookup_record (rh->authority,
                                    rh->private_local_zone,
                                    rlh->record_type,
                                    (char*)rd->data,
                                    rh->priv_key,
                                    rh->timeout,
                                    rh->only_cached,
                                    rlh->proc,
                                    rlh->proc_cls);
        GNUNET_free (rlh);
        GNUNET_CONTAINER_DLL_remove (rlh_head, rlh_tail, rh);
        free_resolver_handle (rh);
        return;
      }
      else
      {
        //Try DNS resolver
        strcpy (rh->dns_name, (char*)rd->data);
        resolve_dns_name (rh);
        return;
      }

    }
    else if (rh->status & RSL_DELEGATE_VPN)
    {
      if (GNUNET_NAMESTORE_TYPE_VPN == rlh->record_type)
      {
        GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
                 "GNS_PHASE_DELEGATE_NS-%llu: Resolved queried VPNRR in NS.\n",
                 rh->id);
        finish_lookup(rh, rlh, rd_count, rd);
        return;
      }
      GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
             "GNS_PHASE_DELEGATE_NS-%llu: VPN delegation starting.\n",
             rh->id);
      GNUNET_assert (NULL != rd);
      rh->proc = &handle_record_vpn;
      resolve_record_vpn (rh, rd_count, rd);
      return;
    }
    else if (rh->status & RSL_DELEGATE_NS)
    {
      if (GNUNET_DNSPARSER_TYPE_NS == rlh->record_type)
      {
        GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		    "GNS_PHASE_DELEGATE_NS-%llu: Resolved queried NSRR in NS.\n",
		    rh->id);
        finish_lookup (rh, rlh, rd_count, rd);
        return;
      }      
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		  "GNS_PHASE_DELEGATE_NS-%llu: NS delegation starting.\n",
		  rh->id);
      GNUNET_assert (NULL != rd);
      rh->proc = &handle_record_ns;
      resolve_record_dns (rh, rd_count, rd);
      return;
    }
    else if (rh->status & RSL_DELEGATE_PKEY)
    {
      if (rh->status & RSL_PKEY_REVOKED)
      {
        GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
                   "GNS_PHASE_DELEGATE_NS-%llu: Resolved PKEY is revoked.\n",
                   rh->id);
        finish_lookup (rh, rlh, 0, NULL);
        return;
      }
      else if (GNUNET_NAMESTORE_TYPE_PKEY == rlh->record_type)
      {
        GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
                   "GNS_PHASE_DELEGATE_NS-%llu: Resolved queried PKEY in NS.\n",
                   rh->id);
        finish_lookup(rh, rlh, rd_count, rd);
        return;
      }
    }
    GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
               "GNS_PHASE_DELEGATE_NS-%llu: Resolving record +\n",
               rh->id);
    strcpy(rh->name, "+\0");
    rh->proc = &handle_record_ns;
    resolve_record_ns(rh);
    return;
  }
  
  if (rh->status & RSL_DELEGATE_NS)
  {
    if (GNUNET_DNSPARSER_TYPE_NS == rlh->record_type)
    {
      GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
                 "GNS_PHASE_DELEGATE_NS-%llu: Resolved queried NSRR in NS.\n",
                 rh->id);
      finish_lookup(rh, rlh, rd_count, rd);
      return;
    }
    
    GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
               "GNS_PHASE_DELEGATE_NS-%llu: NS delegation starting.\n",
               rh->id);
    GNUNET_assert (NULL != rd);
    rh->proc = &handle_record_ns;
    resolve_record_dns (rh, rd_count, rd);
    return;
  }
  
  /**
   * we still have some left
   * check if authority in ns is fresh
   * and exists
   * or we are authority
   **/

  check_dht = GNUNET_YES;
  if ((rh->status & RSL_RECORD_EXISTS) &&
       !(rh->status & RSL_RECORD_EXPIRED))
    check_dht = GNUNET_NO;

  if (0 == GNUNET_CRYPTO_short_hash_cmp(&rh->authority_chain_head->zone,
                                        &rh->private_local_zone))
    check_dht = GNUNET_NO;

  if (GNUNET_YES == rh->only_cached)
    check_dht = GNUNET_NO;

  if (GNUNET_YES == check_dht)
  {

    GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
        "GNS_PHASE_DELEGATE_NS-%llu: Trying to resolve delegation for %s via DHT\n",
        rh->id, rh->name);
    rh->proc = &handle_delegation_dht;
    resolve_delegation_dht(rh);
    return;
  }
  
  if (GNUNET_NO == is_canonical (rh->name))
  {
    /* give up, cannot resolve */
    GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
        "GNS_PHASE_DELEGATE_NS-%llu: Cannot fully resolve delegation for %s!\n",
        rh->id,
        rh->name);
    finish_lookup(rh, rlh, rd_count, rd);
    return;
  }
  GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
             "GNS_PHASE_DELEGATE_NS-%llu: Resolving canonical record %s\n",
             rh->id,
             rh->name);
  rh->proc = &handle_record_ns;
  resolve_record_ns(rh);
}


/**
 * This is a callback function that should give us only PKEY
 * records. Used to query the namestore for the authority (PKEY)
 * for 'name'. It will recursively try to resolve the
 * authority for a given name from the namestore.
 *
 * @param cls the pending query
 * @param key the key of the zone we did the lookup
 * @param expiration expiration date of the record data set in the namestore
 * @param name the name for which we need an authority
 * @param rd_count the number of records with 'name'
 * @param rd the record data
 * @param signature the signature of the authority for the record data
 */
static void
process_delegation_result_ns (void* cls,
			      const struct GNUNET_CRYPTO_EccPublicKey *key,
			      struct GNUNET_TIME_Absolute expiration,
			      const char *name,
			      unsigned int rd_count,
			      const struct GNUNET_NAMESTORE_RecordData *rd,
			      const struct GNUNET_CRYPTO_EccSignature *signature)
{
  struct ResolverHandle *rh = cls;
  struct GNUNET_TIME_Relative remaining_time;
  struct GNUNET_CRYPTO_ShortHashCode zone;
  char new_name[GNUNET_DNSPARSER_MAX_NAME_LENGTH];
  unsigned int i;
  struct GNUNET_TIME_Absolute et;
  struct AuthorityChain *auth;
 
  rh->namestore_task = NULL;
  GNUNET_CRYPTO_short_hash (key,
			    sizeof (struct GNUNET_CRYPTO_EccPublicKey),
			    &zone);
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
	      "GNS_PHASE_DELEGATE_NS-%llu: Got %d records from authority lookup for `%s' in zone %s\n",
	      rh->id, rd_count,
	      name,
	      GNUNET_short_h2s (&zone));

  remaining_time = GNUNET_TIME_absolute_get_remaining (expiration);
  
  rh->status = 0;
  
  if (NULL != name)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "GNS_PHASE_DELEGATE_NS-%llu: Records with name `%s' exist in zone %s.\n",
                rh->id, name,
		GNUNET_short_h2s (&zone));
    rh->status |= RSL_RECORD_EXISTS;
  
    if (0 == remaining_time.rel_value_us)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "GNS_PHASE_DELEGATE_NS-%llu: Record set %s expired.\n",
                  rh->id, name);
      rh->status |= RSL_RECORD_EXPIRED;
    }
  }
  
  /**
   * No authority found in namestore.
   */
  if (0 == rd_count)
  {
    /**
     * We did not find an authority in the namestore
     */
    
    /**
     * No PKEY in zone.
     * Promote this authority back to a name maybe it is
     * our record.
     */
    if (strcmp (rh->name, "") == 0)
    {
      /* simply promote back */
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "GNS_PHASE_DELEGATE_NS-%llu: Promoting %s back to name\n",
                  rh->id, rh->authority_name);
      strcpy (rh->name, rh->authority_name);
    }
    else
    {
      /* add back to existing name */
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "GNS_PHASE_DELEGATE_NS-%llu: Adding %s back to %s\n",
                  rh->id, rh->authority_name, rh->name);
      GNUNET_snprintf (new_name, GNUNET_DNSPARSER_MAX_NAME_LENGTH, "%s.%s",
                       rh->name, rh->authority_name);
      strcpy (rh->name, new_name);
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "GNS_PHASE_DELEGATE_NS-%llu: %s restored\n",
                  rh->id, rh->name);
    }

    rh->proc (rh->proc_cls, rh, 0, NULL);
    return;
  }

  /**
   * We found an authority that may be able to help us
   * move on with query
   * Note only 1 pkey should have been returned.. anything else would be strange
   */
  for (i=0; i < rd_count;i++)
  {
    switch (rd[i].record_type)
    {
    case GNUNET_DNSPARSER_TYPE_CNAME:
      /* Like in regular DNS this should mean that there is no other
       * record for this name.  */

      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		  "GNS_PHASE_DELEGATE_NS-%llu: CNAME `%.*s' found.\n",
		  rh->id,
		  (int) rd[i].data_size,
		  rd[i].data);
      rh->status |= RSL_CNAME_FOUND;
      rh->proc (rh->proc_cls, rh, rd_count, rd);
      return;
    case GNUNET_NAMESTORE_TYPE_VPN:
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		  "GNS_PHASE_DELEGATE_NS-%llu: VPN found.\n",
		  rh->id);
      rh->status |= RSL_DELEGATE_VPN;
      rh->proc (rh->proc_cls, rh, rd_count, rd);
      return;
    case GNUNET_DNSPARSER_TYPE_NS:
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		  "GNS_PHASE_DELEGATE_NS-%llu: NS `%.*s' found.\n",
		  rh->id,
		  (int) rd[i].data_size,
		  rd[i].data);
      rh->status |= RSL_DELEGATE_NS;
      rh->proc (rh->proc_cls, rh, rd_count, rd);
      return;
    case GNUNET_NAMESTORE_TYPE_PKEY:
      rh->status |= RSL_DELEGATE_PKEY;
      if ((ignore_pending_records != 0) &&
	  (rd[i].flags & GNUNET_NAMESTORE_RF_PENDING))
      {
	GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		    "GNS_PHASE_DELEGATE_NS-%llu: PKEY for %s is pending user confirmation.\n",
		    rh->id,
		    name);
	continue;
      }    
      GNUNET_break (0 == (rd[i].flags & GNUNET_NAMESTORE_RF_RELATIVE_EXPIRATION));
      et.abs_value_us = rd[i].expiration_time;
      if (0 == (GNUNET_TIME_absolute_get_remaining (et)).rel_value_us)
      {
	GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		    "GNS_PHASE_DELEGATE_NS-%llu: This pkey is expired.\n",
		    rh->id);
	if (remaining_time.rel_value_us == 0)
	{
	  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		      "GNS_PHASE_DELEGATE_NS-%llu: This dht entry is expired.\n",
		      rh->id);
	  rh->authority_chain_head->fresh = 0;
	  rh->proc (rh->proc_cls, rh, 0, NULL);
	  return;
	}	
	continue;
      }
      /* Resolve rest of query with new authority */
      memcpy (&rh->authority, rd[i].data,
	      sizeof (struct GNUNET_CRYPTO_ShortHashCode));
      auth = GNUNET_malloc(sizeof (struct AuthorityChain));
      auth->zone = rh->authority;
      memset (auth->name, 0, strlen (rh->authority_name)+1);
      strcpy (auth->name, rh->authority_name);
      GNUNET_CONTAINER_DLL_insert (rh->authority_chain_head,
				   rh->authority_chain_tail,
				   auth);
      if (NULL != rh->rd.data)
	GNUNET_free ((void*)(rh->rd.data));      
      memcpy (&rh->rd, &rd[i], sizeof (struct GNUNET_NAMESTORE_RecordData));
      rh->rd.data = GNUNET_malloc (rd[i].data_size);
      memcpy ((void*)rh->rd.data, rd[i].data, rd[i].data_size);
      rh->rd_count = 1;
      /* Check for key revocation and delegate */
      rh->namestore_task = GNUNET_NAMESTORE_lookup_record (namestore_handle,
							   &rh->authority,
							   GNUNET_GNS_MASTERZONE_STR,
							   GNUNET_NAMESTORE_TYPE_REV,
							   &process_pkey_revocation_result_ns,
							   rh);
      return;
    default:
      /* ignore, move to next result */
      break;
    }
  }
  
  /* no answers that would cause delegation were found */
  GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
	     "GNS_PHASE_DELEGATE_NS-%llu: Authority lookup failed (no PKEY record)\n", 
	     rh->id);
  /**
   * If we have found some records for the LAST label
   * we return the results. Else NULL.
   */
  if (0 == strcmp (rh->name, ""))
  {
    /* Start shortening */
    if ((rh->priv_key != NULL) &&
        (is_canonical (rh->name) == GNUNET_YES))
    {
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "GNS_PHASE_DELEGATE_NS-%llu: Trying to shorten authority chain\n",
              rh->id);
      start_shorten (rh->authority_chain_head,
                    rh->priv_key);
    }
    /* simply promote back */
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "GNS_PHASE_DELEGATE_NS-%llu: Promoting %s back to name\n",
                rh->id, rh->authority_name);
    strcpy (rh->name, rh->authority_name);
    rh->proc (rh->proc_cls, rh, rd_count, rd);
  }
  else
  {
    GNUNET_snprintf (new_name, GNUNET_DNSPARSER_MAX_NAME_LENGTH,
                     "%s.%s", rh->name, rh->authority_name);
    strcpy (rh->name, new_name);
    rh->proc (rh->proc_cls, rh, 0, NULL);
  }
}


/**
 * Resolve the delegation chain for the request in our namestore
 * (as in, find the respective authority for the leftmost label).
 *
 * @param rh the resolver handle
 */
static void
resolve_delegation_ns (struct ResolverHandle *rh)
{
  pop_tld (rh->name, rh->authority_name);
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
	      "GNS_PHASE_DELEGATE_NS-%llu: Finding authority for `%s' by looking up `%s' in GADS zone `%s'\n",
	      rh->id,	      
	      rh->name,
	      rh->authority_name,
	      GNUNET_short_h2s (&rh->authority));
  rh->namestore_task = GNUNET_NAMESTORE_lookup_record (namestore_handle,
						       &rh->authority,
						       rh->authority_name,
						       GNUNET_DNSPARSER_TYPE_ANY,
						       &process_delegation_result_ns,
						       rh);
}

#endif



/**
 * Lookup of a record in a specific zone calls lookup result processor
 * on result.
 *
 * @param zone the zone to perform the lookup in
 * @param record_type the record type to look up
 * @param name the name to look up
 * @param shorten_key a private key for use with PSEU import (can be NULL)
 * @param only_cached GNUNET_NO to only check locally not DHT for performance
 * @param proc the processor to call on result
 * @param proc_cls the closure to pass to @a proc
 * @return handle to cancel operation
 */
struct GNS_ResolverHandle *
GNS_resolver_lookup (const struct GNUNET_CRYPTO_EccPublicKey *zone,
		     uint32_t record_type,
		     const char *name,
		     const struct GNUNET_CRYPTO_EccPrivateKey *shorten_key,
		     int only_cached,
		     GNS_ResultProcessor proc, void *proc_cls)
{
  return NULL;
#if 0
  struct ResolverHandle *rh;
  struct RecordLookupHandle* rlh;
  char string_hash[GNUNET_DNSPARSER_MAX_LABEL_LENGTH];
  char nzkey[GNUNET_DNSPARSER_MAX_LABEL_LENGTH];
  char* nzkey_ptr = nzkey;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Starting resolution for `%s' (type=%d) with timeout %s!\n",
              name, record_type,
	      GNUNET_STRINGS_relative_time_to_string (timeout, GNUNET_YES));

  if ((is_canonical ((char*)name) == GNUNET_YES) &&
      (strcmp(GNUNET_GNS_TLD, name) != 0))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "%s is canonical and not gnunet -> cannot resolve!\n", name);
    proc(cls, 0, NULL);
    return;
  }
  
  rlh = GNUNET_malloc (sizeof(struct RecordLookupHandle));
  rh = GNUNET_malloc (sizeof (struct ResolverHandle));
  rh->authority = zone;
  rh->id = rid_gen++;
  rh->proc_cls = rlh;
  rh->priv_key = key;
  rh->timeout = timeout;
  rh->private_local_zone = pzone;
  rh->only_cached = only_cached;

  GNUNET_CONTAINER_DLL_insert (rlh_head, rlh_tail, rh);
  
  if (NULL == key)
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "No shorten key for resolution\n");

  if (timeout.rel_value_us != GNUNET_TIME_UNIT_FOREVER_REL.rel_value_us)
  {
    /*
     * Set timeout for authority lookup phase to 1/2
     */
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "Timeout for lookup set to %s/2\n", 
		GNUNET_STRINGS_relative_time_to_string (rh->timeout, GNUNET_YES));
    rh->timeout_task = GNUNET_SCHEDULER_add_delayed (
                                GNUNET_TIME_relative_divide(timeout, 2),
                                                &handle_lookup_timeout,
                                                rh);
    rh->timeout_cont = &dht_authority_lookup_timeout;
    rh->timeout_cont_cls = rh;
  }
  else
  {
    GNUNET_log(GNUNET_ERROR_TYPE_DEBUG, "No timeout for query!\n");
    rh->timeout_task = GNUNET_SCHEDULER_NO_TASK;
  }
  
  if (strcmp(GNUNET_GNS_TLD, name) == 0)
  {
    /**
     * Only '.gads' given
     */
    strcpy (rh->name, "\0");
  }
  else
  {
    if (is_zkey_tld(name) == GNUNET_YES)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "TLD is zkey\n");
      /**
       * This is a zkey tld
       * build hash and use as initial authority
       */
      memset(rh->name, 0,
             strlen(name)-strlen(GNUNET_GNS_TLD_ZKEY));
      memcpy(rh->name, name,
             strlen(name)-strlen(GNUNET_GNS_TLD_ZKEY) - 1);
      pop_tld (rh->name, string_hash);

      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "ZKEY is %s!\n", string_hash);
      
      GNUNET_STRINGS_utf8_toupper(string_hash, &nzkey_ptr);

      if (GNUNET_OK != GNUNET_CRYPTO_short_hash_from_string(nzkey,
                                                      &rh->authority))
      {
        GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                    "Cannot convert ZKEY `%s' to hash!\n", string_hash);
        
	if (GNUNET_SCHEDULER_NO_TASK != rh->timeout_task)
	  GNUNET_SCHEDULER_cancel (rh->timeout_task);
        GNUNET_CONTAINER_DLL_remove (rlh_head, rlh_tail, rh);
        GNUNET_free (rh);
        GNUNET_free (rlh);
        proc (cls, 0, NULL);
        return;
      }

    }
    else if (is_gads_tld (name) == GNUNET_YES)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "TLD is gads\n");
      /**
       * Presumably GADS tld
       */
      memcpy (rh->name, name,
              strlen (name) - strlen(GNUNET_GNS_TLD) - 1);
      rh->name[strlen (name) - strlen(GNUNET_GNS_TLD) - 1] = '\0';
    }
    else
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                  _("Not a GADS TLD: `%s'\n"), 
		  name);      
      if (GNUNET_SCHEDULER_NO_TASK != rh->timeout_task)
        GNUNET_SCHEDULER_cancel (rh->timeout_task);
      GNUNET_CONTAINER_DLL_remove (rlh_head, rlh_tail, rh);
      GNUNET_free (rh);
      GNUNET_free (rlh);
      proc (cls, 0, NULL);
      return;
    }
  }
  
  /**
   * Initialize authority chain
   */
  rh->authority_chain_head = GNUNET_malloc (sizeof(struct AuthorityChain));
  rh->authority_chain_tail = rh->authority_chain_head;
  rh->authority_chain_head->zone = rh->authority;
  strcpy (rh->authority_chain_head->name, "");
  
  /**
   * Copy original query into lookup handle
   */
  rlh->record_type = record_type;
  memset(rlh->name, 0, strlen(name) + 1);
  strcpy(rlh->name, name);
  rlh->proc = proc;
  rlh->proc_cls = cls;

  rh->proc = &handle_delegation_ns;
  resolve_delegation_ns (rh);
#endif
}



/**
 * Cancel active resolution (i.e. client disconnected).
 *
 * @param h resolution to abort
 */
void
GNS_resolver_lookup_cancel (struct GNS_ResolverHandle *h)
{
}


/* ***************** Resolver initialization ********************* */


/**
 * Initialize the resolver
 *
 * @param nh the namestore handle
 * @param dh the dht handle
 * @param c configuration handle
 * @param max_bg_queries maximum number of parallel background queries in dht
 * @param ignore_pending ignore records that still require user confirmation
 *        on lookup
 */
void
GNS_resolver_init (struct GNUNET_NAMESTORE_Handle *nh,
		   struct GNUNET_DHT_Handle *dh,
		   const struct GNUNET_CONFIGURATION_Handle *c,
		   unsigned long long max_bg_queries)
{
  char *dns_ip;

  cfg = c;
  namestore_handle = nh;
  dht_handle = dh;
  dht_lookup_heap =
    GNUNET_CONTAINER_heap_create (GNUNET_CONTAINER_HEAP_ORDER_MIN);
  max_allowed_background_queries = max_bg_queries;
  if (GNUNET_OK !=
      GNUNET_CONFIGURATION_get_value_string (c,
					     "gns",
					     "DNS_RESOLVER",
					     &dns_ip))
  {
    /* user did not specify DNS resolver, use 8.8.8.8 */
    dns_ip = GNUNET_strdup ("8.8.8.8");
  }
  dns_handle = GNUNET_DNSSTUB_start (dns_ip);
  GNUNET_free (dns_ip);
}


/**
 * Shutdown resolver
 */
void
GNS_resolver_done ()
{
  while (NULL != gph_head)
    free_get_pseu_authority_handle (gph_head);
  GNUNET_CONTAINER_heap_destroy (dht_lookup_heap);
  dht_lookup_heap = NULL;
  GNUNET_DNSSTUB_stop (dns_handle);
  dns_handle = NULL;
}


/* *************** common helper functions (do not really belong here) *********** */

/**
 * Checks if "name" ends in ".tld"
 *
 * @param name the name to check
 * @param tld the TLD to check for
 * @return GNUNET_YES or GNUNET_NO
 */
int
is_tld (const char* name, const char* tld)
{
  size_t offset = 0;

  if (strlen (name) <= strlen (tld))
    return GNUNET_NO;
  offset = strlen (name) - strlen (tld);
  if (0 != strcmp (name + offset, tld))
    return GNUNET_NO;
  return GNUNET_YES;
}




/* end of gnunet-service-gns_resolver.c */
